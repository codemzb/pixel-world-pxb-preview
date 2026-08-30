// regutil.cpp — .pxb file-association management (Windows only).
//
#include "regutil.h"
#include "pxb_reg_contract.h"
#include "fileutil.h"   // pxb::utf8_to_wide / pxb::wide_to_utf8 (single source)
#include "i18n.h"       // tr() — error strings surface in UI message boxes

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>     // SHChangeNotify
#include <cstdio>
#include <string>
#include <vector>

namespace pxb {
namespace {

// UTF-8 <-> wide conversions are provided by fileutil.cpp (pxb::utf8_to_wide /
// pxb::wide_to_utf8) — call those directly; do not re-implement here.

std::string exe_dir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::wstring w(buf, n);
    size_t slash = w.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    return wide_to_utf8(w.substr(0, slash));
}

std::wstring exe_dir_w() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::wstring w(buf, n);
    size_t slash = w.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    return w.substr(0, slash + 1);
}

std::wstring exe_path_w() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::wstring(buf, n);
}

// Read a REG_SZ default (or named) value. Returns false when absent.
bool read_reg_sz(HKEY root, const std::wstring& sub, const wchar_t* value,
                 std::wstring& out) {
    HKEY hk = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &hk) != ERROR_SUCCESS)
        return false;
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    DWORD type = 0;
    LONG r = RegQueryValueExW(hk, value, nullptr, &type, (LPBYTE)buf, &size);
    RegCloseKey(hk);
    if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return false;
    // RegQueryValueExW does not always NUL-terminate REG_SZ data. When the
    // value exactly fills the buffer (chars == 1024) there is no room for a
    // terminator: only accept it if the last slot is already NUL, otherwise
    // treat the key as absent. (The old unconditional `buf[size/2] = L'\0'`
    // wrote buf[1024] — one element past the end — in exactly that case.)
    size_t chars = size / sizeof(wchar_t);
    if (chars == 0 || chars > 1024) return false;
    if (chars == 1024) {
        if (buf[1023] != L'\0') return false;
        chars = 1023;
    }
    buf[chars] = L'\0';   // defensive terminate (index <= 1023)
    out = buf;
    return true;
}

bool key_exists(HKEY root, const std::wstring& sub) {
    HKEY hk = nullptr;
    LONG r = RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &hk);
    if (r == ERROR_SUCCESS) RegCloseKey(hk);
    return r == ERROR_SUCCESS;
}

void set_reg_sz(HKEY root, const std::wstring& sub, const wchar_t* value,
                const std::wstring& data) {
    HKEY hk = nullptr;
    if (RegCreateKeyExW(root, sub.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &hk, nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExW(hk, value, 0, REG_SZ, (const BYTE*)data.c_str(),
                   (DWORD)((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hk);
}

void delete_reg_tree(HKEY root, const std::wstring& sub) {
    RegDeleteTreeW(root, sub.c_str());
}

// Windows paths are case-insensitive; compare without forcing a copy.
bool path_equals_ci(const std::string& a, const std::string& b) {
    return _stricmp(a.c_str(), b.c_str()) == 0;
}

// Refresh Explorer's association + thumbnail caches the polite way (no
// explorer.exe restart — that would nuke the user's open windows).
void refresh_explorer() {
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_FLUSHNOWAIT, nullptr, nullptr);
}

std::string last_error_text(const char* what) {
    DWORD e = GetLastError();
    char buf[64];
    snprintf(buf, sizeof(buf), "%s (error %lu)", what, (unsigned long)e);
    return buf;
}

} // namespace

// ---- public ----------------------------------------------------------------

// The "open" verb (what makes double-clicking a .pxb launch pxb-preview) can
// live under HKCU — no administrator token required, and it applies only to the
// current user. The thumbnail COM handler, however, MUST stay in HKLM because
// Explorer.exe (a system process) only loads COM objects from machine-wide
// registration; HKCU COM keys are ignored for shell thumbnail preview.
//
// We therefore split the contract:
//   * .pxb ProgID + open verb  -> HKCU  (no elevation, user-scoped)
//   * CLSID/InprocServer32      -> HKLM (elevation, provides thumbnails)
//
// query() only checks HKCU for the "double-click opens pxb-preview" state;
// HKLM is inspected separately just for the thumbnail CLSID. This prevents
// stale admin-installed HKCU entries from making unregister appear to fail.

// Look up the open-verb command for the CURRENT USER only. New registrations
// always write the open verb to HKCU; HKLM may contain stale entries from
// earlier admin installs and must NOT make query() report "still registered".
static bool read_open_verb(std::wstring& val) {
    std::wstring verb_key = std::wstring(L"Software\\Classes\\") + PXB_PROGID_W +
                            L"\\shell\\open\\command";
    return read_reg_sz(HKEY_CURRENT_USER, verb_key, nullptr, val);
}

// Look up the .pxb ProgID default for the CURRENT USER only.
static bool read_progid(std::wstring& val) {
    std::wstring ext_key = std::wstring(L"Software\\Classes\\") + PXB_EXT_W;
    return read_reg_sz(HKEY_CURRENT_USER, ext_key, nullptr, val);
}

RegState query_pxb_registration() {
    RegState st;
    std::wstring val;

    // .pxb default -> PxbPreview.pxbfile  (either root)
    if (read_progid(val) && val == PXB_PROGID_W)
        st.progid_registered = true;

    // <ProgID>\shell\open\command  (either root)
    if (read_open_verb(val)) {
        st.open_verb_ok = true;
        // The stored value is `"<exe>" "%1"`; extract the exe path (first
        // quoted token) for display.
        std::string raw = wide_to_utf8(val);
        size_t q1 = raw.find('"');
        if (q1 != std::string::npos) {
            size_t q2 = raw.find('"', q1 + 1);
            st.exe_path = (q2 != std::string::npos) ? raw.substr(q1 + 1, q2 - q1 - 1)
                                                    : raw.substr(q1 + 1);
        } else {
            st.exe_path = raw;
        }
    }

    // CLSID InprocServer32 (HKLM only — COM is machine-wide)
    std::wstring clsid_key = std::wstring(L"Software\\Classes\\CLSID\\") +
                             PXB_CLSID_STR_W + L"\\InprocServer32";
    if (read_reg_sz(HKEY_LOCAL_MACHINE, clsid_key, nullptr, val))
        st.clsid_ok = true;

    // HKLM open verb leftover from older admin installs. We don't manage this
    // tier anymore (it would re-introduce the elevation requirement), but if it
    // still points at OUR install, double-clicking a .pxb would still launch
    // us — so the UI surfaces it and unregister may ask for elevation.
    std::wstring lm_verb = std::wstring(L"Software\\Classes\\") + PXB_PROGID_W +
                           L"\\shell\\open\\command";
    std::wstring lm_cmd;
    if (read_reg_sz(HKEY_LOCAL_MACHINE, lm_verb, nullptr, lm_cmd)) {
        // The leftover verb "points at our install" when its command string
        // begins with our exe directory. (exe_path_w() is a prefix of this, so
        // checking the directory covers both the exe and a sibling helper.)
        // A single readable test replaces the previous two-branch expression,
        // whose second branch was unreachable given the first already covered
        // any command starting with our directory.
        std::wstring self_dir = exe_dir_w();   // trailing backslash
        if (!self_dir.empty() && lm_cmd.find(self_dir) == 0) {
            st.hklm_open_verb = true;
        }
    }

    // Per-user UserChoice (HKCU) silently wins over ProgID for double-click.
    std::wstring uc = L"Software\\Microsoft\\Windows\\CurrentVersion\\"
                      L"Explorer\\FileExts\\.pxb\\UserChoice";
    st.userchoice_override = key_exists(HKEY_CURRENT_USER, uc);

    return st;
}

std::string thumbnail_dll_path() {
    const std::string dir = exe_dir();
    if (dir.empty()) return {};

    std::vector<std::string> candidates;
    candidates.push_back(dir + "\\PxbThumbnailHandler.dll");
    candidates.push_back(dir + "\\build_thumb\\PxbThumbnailHandler.dll");

    // Walk up to 3 parents (source-tree layouts: build/ + build_thumb/).
    std::string cur = dir;
    for (int i = 0; i < 3; ++i) {
        size_t slash = cur.find_last_of("\\/");
        if (slash == std::string::npos) break;
        cur = cur.substr(0, slash);
        candidates.push_back(cur + "\\build_thumb\\PxbThumbnailHandler.dll");
        candidates.push_back(cur + "\\PxbThumbnailHandler.dll");
    }

    for (const auto& c : candidates) {
        DWORD attr = GetFileAttributesW(utf8_to_wide(c).c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
            return c;
    }
    return {};
}

bool is_elevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION te;
    DWORD got = 0;
    BOOL ok = GetTokenInformation(tok, TokenElevation, &te, sizeof(te), &got);
    CloseHandle(tok);
    return ok && got == sizeof(te) && te.TokenIsElevated != 0;
}

bool register_pxb_association(std::string& err) {
    err.clear();

    // Tier 1 (no elevation, current-user): the ProgID + open verb that makes
    // double-clicking a .pxb launch THIS exe. This is the part the user always
    // wants and it never needs an administrator token.
    std::wstring ext_key = std::wstring(L"Software\\Classes\\") + PXB_EXT_W;
    std::wstring progid_key = std::wstring(L"Software\\Classes\\") + PXB_PROGID_W;
    std::wstring verb_key = progid_key + L"\\shell\\open\\command";
    std::wstring open_key = progid_key + L"\\shell\\open";
    std::wstring cmd = L"\"" + exe_path_w() + L"\" \"%1\"";
    set_reg_sz(HKEY_CURRENT_USER, ext_key, nullptr, PXB_PROGID_W);
    set_reg_sz(HKEY_CURRENT_USER, progid_key, nullptr, L"PXB Pixel Art");
    set_reg_sz(HKEY_CURRENT_USER, open_key, nullptr, L"Open");
    set_reg_sz(HKEY_CURRENT_USER, verb_key, nullptr, cmd);

    // Clear a stale per-user override so our ProgID wins for double-click.
    delete_reg_tree(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\"
                    L"Explorer\\FileExts\\.pxb\\UserChoice");

    // Tier 2 (elevation, machine-wide): the thumbnail COM handler. Explorer
    // only loads COM objects from HKLM, so thumbnails require an admin token.
    // If we are not elevated, we SKIP this tier instead of failing — the
    // double-click association above already succeeded for the current user.
    bool thumb_ok = true;
    if (is_elevated()) {
        std::string dll = thumbnail_dll_path();
        if (!dll.empty()) {
            HMODULE h = LoadLibraryW(utf8_to_wide(dll).c_str());
            if (h) {
                auto fn = (HRESULT(WINAPI*)())GetProcAddress(h, "DllRegisterServer");
                if (fn) {
                    HRESULT hr = fn();
                    if (FAILED(hr)) thumb_ok = false;
                } else {
                    thumb_ok = false;
                }
                FreeLibrary(h);
            }
        }
        // NOTE: We intentionally do NOT write the open verb to HKLM. The open
        // association is per-user (HKCU); leaving an HKLM open verb behind would
        // cause unregister to fail its post-condition when HKCU is cleaned but
        // query still sees the HKLM fallback.
    }

    // Verify the user-scoped open verb actually took effect. A HKCU write
    // essentially never fails, but check anyway for a trustworthy signal.
    RegState st = query_pxb_registration();
    std::string want = wide_to_utf8(exe_path_w());
    if (!st.registered() || !path_equals_ci(st.exe_path, want)) {
        err = tr(Str::AssocRegNotEffective);
        return false;
    }

    // Surface a non-fatal note when thumbnails could not be installed (no admin).
    if (!thumb_ok)
        err = tr(Str::AssocThumbAdminNote);

    refresh_explorer();
    return true;
}

bool unregister_pxb_association(std::string& err) {
    err.clear();

    // Tier 1 (no elevation, current-user): remove the ProgID + open verb. This
    // is what makes double-click stop using pxb-preview, and it never needs an
    // administrator token.
    std::wstring ext_key = std::wstring(L"Software\\Classes\\") + PXB_EXT_W;
    std::wstring progid_key = std::wstring(L"Software\\Classes\\") + PXB_PROGID_W;
    delete_reg_tree(HKEY_CURRENT_USER, progid_key);
    // Only drop the .pxb -> ProgID mapping if it still points at us (don't
    // clobber another program's association that happens to also use .pxb).
    std::wstring cur;
    if (read_reg_sz(HKEY_CURRENT_USER, ext_key, nullptr, cur) && cur == PXB_PROGID_W)
        delete_reg_tree(HKEY_CURRENT_USER, ext_key);

    // Clear the per-user UserChoice override we may have created.
    delete_reg_tree(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\"
                    L"Explorer\\FileExts\\.pxb\\UserChoice");

    // Tier 2 (elevation, machine-wide): remove the thumbnail COM handler and
    // any HKLM ProgID/open-verb leftovers from older admin installs. Only
    // attempted when elevated; otherwise silently skipped.
    if (is_elevated()) {
        std::string dll = thumbnail_dll_path();
        if (!dll.empty()) {
            HMODULE h = LoadLibraryW(utf8_to_wide(dll).c_str());
            if (h) {
                auto fn = (HRESULT(WINAPI*)())GetProcAddress(h, "DllUnregisterServer");
                if (fn) {
                    HRESULT hr = fn();
                    (void)hr;   // best-effort; post-condition checks reality
                }
                FreeLibrary(h);
            }
        }
        // Clean up stale HKLM open-association keys that old versions (or the
        // previous HKLM-on-admin design) may have left behind. We only touch
        // keys that clearly belong to us.
        std::wstring lm_ext_key = std::wstring(L"Software\\Classes\\") + PXB_EXT_W;
        std::wstring lm_progid_key = std::wstring(L"Software\\Classes\\") + PXB_PROGID_W;
        if (read_reg_sz(HKEY_LOCAL_MACHINE, lm_ext_key, nullptr, cur) &&
            cur == PXB_PROGID_W) {
            delete_reg_tree(HKEY_LOCAL_MACHINE, lm_ext_key);
        }
        delete_reg_tree(HKEY_LOCAL_MACHINE, lm_progid_key);
    }

    // Post-condition: the current-user open verb must be gone. query() only
    // checks HKCU, so a leftover HKLM open verb no longer makes this fail.
    RegState st = query_pxb_registration();
    if (st.registered()) {
        err = tr(Str::AssocUnregNotEffective);
        return false;
    }

    // If a system-wide (HKLM) open verb still points at our exe, the current
    // user is clean but double-clicking .pxb elsewhere may still launch us.
    // That requires elevation to remove — report it as a clear note, not a
    // hard failure, and only when we could not elevate.
    if (st.hklm_open_verb) {
        if (is_elevated()) {
            // We should have removed it above; if it still lingers, report it.
            err = tr(Str::AssocHklmStillLingers);
        } else {
            err = tr(Str::AssocHklmNeedsAdmin);
        }
    }

    refresh_explorer();
    return true;
}

} // namespace pxb

#else // !_WIN32 — stubs so the app still compiles on non-Windows targets.

namespace pxb {
RegState query_pxb_registration() { return {}; }
std::string thumbnail_dll_path() { return {}; }
bool is_elevated() { return false; }
bool register_pxb_association(std::string& err) { err = "not supported on this platform"; return false; }
bool unregister_pxb_association(std::string& err) { err = "not supported on this platform"; return false; }
} // namespace pxb

#endif // _WIN32
