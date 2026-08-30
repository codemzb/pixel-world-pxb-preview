// fileutil.cpp
#include "fileutil.h"
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#include <fstream>
#include <cstdint>
#endif

namespace fs = std::filesystem;

namespace pxb {

static std::string to_lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

#ifdef _WIN32
// Convert a wide path to a UTF-8 std::string. std::filesystem::path::string()
// on libstdc++ MinGW uses the current LC_CTYPE locale to convert from
// wchar_t -> multibyte. With the default "C" locale, non-ASCII characters
// (e.g. Chinese filenames) collapse to '?' and the original bytes are lost.
// Doing the conversion ourselves with WideCharToMultiByte(CP_UTF8) always
// produces a lossless UTF-8 byte sequence, which ImGui can render once a
// CJK font is loaded.
//
// This is the shared implementation backing the declarations in fileutil.h;
// the registry helper and UI layer call pxb::wide_to_utf8 / pxb::utf8_to_wide
// rather than re-implementing the conversion.
std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], len,
                        nullptr, nullptr);
    return out;
}

// Inverse of wide_to_utf8: UTF-8 std::string -> native wide string (lossless).
// MB_ERR_INVALID_CHARS makes a malformed UTF-8 input return an empty string
// instead of silently producing garbage (matches the stricter contract the
// registry helper relied on before this was centralized).
std::wstring utf8_to_wide(const std::string& u8) {
    if (u8.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, u8.c_str(),
                                   (int)u8.size(), nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring w(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, u8.c_str(),
                        (int)u8.size(), &w[0], wlen);
    return w;
}

// Build a std::filesystem::path from a UTF-8 path WITHOUT going through the
// narrow-encoding locale round-trip (which is exactly what mangles Chinese
// paths on MinGW). We decode UTF-8 -> UTF-16 ourselves and let fs::path own
// the wide representation directly.
static fs::path utf8_to_path(const std::string& u8) {
    std::wstring w = utf8_to_wide(u8);
    return w.empty() ? fs::path() : fs::path(w);
}
#endif

// UTF-8-safe split: only ASCII separators ('/' and '\\') are treated as
// delimiters. UTF-8 multibyte sequences never contain these bytes, so this is
// correct for paths in any language without touching std::filesystem at all.
static size_t last_sep(const std::string& s) {
    size_t p = std::string::npos;
    size_t q = s.find_last_of('/');
    if (q != std::string::npos) p = q;
    q = s.find_last_of('\\');
    if (q != std::string::npos && q > p) p = q;
    return p;
}

std::vector<FileEntry> list_directory(const std::string& path) {
    std::vector<FileEntry> dirs, files;
    std::error_code ec;
#ifdef _WIN32
    fs::path dirp = utf8_to_path(path);
#else
    fs::path dirp = fs::u8path(path);
#endif
    for (const auto& it : fs::directory_iterator(dirp, ec)) {
        FileEntry e;
#ifdef _WIN32
        e.name = wide_to_utf8(it.path().filename().wstring());
#else
        e.name = it.path().filename().u8string();
#endif
        std::error_code ec2;
        e.is_dir = it.is_directory(ec2);
        e.size = e.is_dir ? 0 : (uint64_t)it.file_size(ec2);
        if (e.is_dir) dirs.push_back(e);
        else files.push_back(e);
    }
    auto cmp = [](const FileEntry& a, const FileEntry& b) {
        return to_lower(a.name) < to_lower(b.name);
    };
    std::sort(dirs.begin(), dirs.end(), cmp);
    std::sort(files.begin(), files.end(), cmp);
    dirs.insert(dirs.end(), files.begin(), files.end());
    return dirs;
}

std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    // Trim trailing separators from `a` and leading separators from `b`, then
    // join with a single '/'. UTF-8 safe (separators are ASCII).
    std::string a2 = a;
    while (!a2.empty() && (a2.back() == '/' || a2.back() == '\\')) a2.pop_back();
    std::string b2 = b;
    while (!b2.empty() && (b2.front() == '/' || b2.front() == '\\')) b2.erase(0, 1);
    return a2 + "/" + b2;
}

std::string path_filename(const std::string& path) {
    size_t p = last_sep(path);
    return (p == std::string::npos) ? path : path.substr(p + 1);
}

std::string path_parent(const std::string& path) {
    size_t p = last_sep(path);
    if (p == std::string::npos) return ".";
    if (p == 0) return path.substr(0, 1);   // e.g. "/foo" -> "/"
    return path.substr(0, p);
}

std::string path_extension(const std::string& path) {
    std::string name = path_filename(path);
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return "";
    return to_lower(name.substr(dot));
}

bool path_exists(const std::string& path) {
#ifdef _WIN32
    // Bypass std::filesystem locale handling entirely: UTF-8 -> UTF-16 ->
    // GetFileAttributesW, which accepts any Unicode path regardless of locale.
    std::wstring w = utf8_to_wide(path);
    if (w.empty()) return false;
    DWORD attr = GetFileAttributesW(w.c_str());
    return attr != INVALID_FILE_ATTRIBUTES;
#else
    std::error_code ec;
    return fs::exists(fs::u8path(path), ec);
#endif
}

bool is_pxb_file(const std::string& name) {
    return path_extension(name) == ".pxb";
}

// Read an entire file into memory. UTF-8 path on all platforms. On failure
// returns an empty vector and (if `err` is provided) a human-readable reason.
// Used by image::load_png_file and pxb::read_pxb_file.
// (Why Win32 CreateFileW instead of std::ifstream — see fileutil.h header.)
std::vector<uint8_t> read_file_bytes(const std::string& path, std::string* err) {
    std::vector<uint8_t> out;
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(),
                                   (int)path.size(), nullptr, 0);
    if (wlen <= 0) {
        if (err) *err = "invalid UTF-8 path: " + path;
        return out;
    }
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(),
                        (int)path.size(), &wpath[0], wlen);
    HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (err) *err = "cannot open file: " + path;
        return out;
    }
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || sz.QuadPart > (1LL << 30)) {
        CloseHandle(h);
        if (err) *err = "file too large (>1GB)";
        return out;
    }
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0;
    BOOL ok = ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr);
    CloseHandle(h);
    if (!ok || (size_t)got != out.size()) {
        if (err) *err = "short read: " + path;
        out.clear();
    }
#else
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open file: " + path;
        return out;
    }
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
#endif
    return out;
}

std::vector<uint8_t> read_file_bytes(const std::string& path) {
    return read_file_bytes(path, nullptr);
}

bool file_stat(const std::string& path, int64_t* mtime_ns, uint64_t* size) {
#ifdef _WIN32
    std::wstring w = utf8_to_wide(path);
    if (w.empty()) return false;
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(w.c_str(), GetFileExInfoStandard, &d)) return false;
    if (size) *size = ((uint64_t)d.nFileSizeHigh << 32) | (uint64_t)d.nFileSizeLow;
    if (mtime_ns) {
        // FILETIME = 100-ns intervals since 1601-01-01; Unix epoch = 1970-01-01.
        uint64_t ft = ((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32) |
                      (uint64_t)d.ftLastWriteTime.dwLowDateTime;
        *mtime_ns = (int64_t)((ft - 116444736000000000ULL) * 100ULL);
    }
    return true;
#else
    std::error_code ec;
    auto p = fs::u8path(path);
    if (!fs::exists(p, ec)) return false;
    if (size) *size = (uint64_t)fs::file_size(p, ec);
    if (mtime_ns && !ec) {
        auto tt = fs::last_write_time(p, ec);
        if (!ec) {
            auto ns = std::chrono::time_point_cast<std::chrono::nanoseconds>(tt).time_since_epoch();
            *mtime_ns = ns.count();
        }
    }
    return true;
#endif
}

} // namespace pxb
