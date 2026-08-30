// regutil.h — .pxb file-association management (Windows only).
//
// The registry contract (CLSID, ProgID, shell keys) is owned by
// PxbThumbnailHandler.dll — see src/core/pxb_reg_contract.h. This module
// never duplicates those identifiers: it either queries them read-only, or
// invokes the DLL's DllRegisterServer / DllUnregisterServer (the same exports
// regsvr32 calls), so the app and the DLL can never drift.
//
// Two-tier contract:
//   * .pxb ProgID + open verb  -> HKCU  (no elevation, user-scoped; makes
//     double-clicking a .pxb open THIS exe). This is the default, admin-free
//     behavior the UI uses on every register/unregister.
//   * CLSID/InprocServer32      -> HKLM (elevation required; provides Explorer
//     thumbnail preview, which only loads COM objects machine-wide). This tier
//     is best-effort: if not elevated, registration still succeeds for the
//     current user and only thumbnails are skipped (surfaced as a note).
//
#pragma once

#include <string>

namespace pxb {

// Read-only snapshot of the current .pxb association state. All fields are
// queried from HKLM/HKCU; nothing is written.
struct RegState {
    bool progid_registered = false;    // HKCU: .pxb -> PxbPreview.pxbfile
    bool open_verb_ok     = false;     // HKCU: <ProgID>\shell\open\command exists
    bool hklm_open_verb   = false;     // HKLM: open verb still points at OUR exe
    bool clsid_ok         = false;     // CLSID\...\InprocServer32 exists (HKLM only)
    bool userchoice_override = false;  // HKCU FileExts UserChoice would beat ProgID
    std::string exe_path;              // what the (current-user) open verb points to

    // The current-user open verb is the contract we manage. HKLM leftovers from
    // older admin installs are reported separately so the UI can tell the user
    // to re-run as administrator instead of silently failing to "unregister".
    bool registered() const { return progid_registered && open_verb_ok; }
};

// Read HKLM/HKCU; never writes. Cheap enough to call every time the File menu
// is opened so the menu state always reflects reality.
RegState query_pxb_registration();

// Locate PxbThumbnailHandler.dll: exe dir first, then exe-dir\build_thumb,
// then up to 3 parent levels (mirrors the old install.bat layout rules).
// Returns empty if not found.
std::string thumbnail_dll_path();

// True when this process holds an elevated (administrator) token.
bool is_elevated();

// Perform register / unregister in-process. The current-user open verb is
// always written (no elevation); the machine-wide thumbnail COM handler is
// attempted only when elevated and is non-fatal on failure. On success the
// Explorer association cache is refreshed (SHChangeNotify). Returns true when
// the user-scoped association is in effect; err may carry a non-fatal note
// (e.g. thumbnails skipped due to no elevation) even when true.
bool register_pxb_association(std::string& err);
bool unregister_pxb_association(std::string& err);

} // namespace pxb
