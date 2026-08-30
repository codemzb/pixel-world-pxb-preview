// fileutil.h
//
// Minimal cross-platform filesystem helpers built on C++17 <filesystem>.
// Kept intentionally small: the app implements its own in-window file browser
// so no external file-dialog dependency is required.
//
// === UTF-8 path caveat (single source of truth) ============================
// On MinGW-w64 the libstdc++ <filesystem> / std::ifstream implementation uses
// the *ANSI* code page (LC_CTYPE "C") for narrow strings. A UTF-8 byte
// sequence containing non-ASCII (e.g. a Chinese filename) gets mangled during
// the multibyte -> wchar_t round-trip and the open call silently fails.
// Every file read in this project therefore goes through read_file_bytes(),
// which decodes the UTF-8 path to UTF-16 and calls Win32 CreateFileW (which
// accepts any Unicode path regardless of locale). Do NOT re-open files via
// std::ifstream / std::filesystem on Windows — that is the bug we centralised
// here. See also pxb_reader.cpp and main.cpp (utf8_argv).
// ===========================================================================
//
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace pxb {

struct FileEntry {
    std::string name;
    bool is_dir = false;
    uint64_t size = 0;
};

// List a directory. Directories are returned before files; each group is
// sorted alphabetically (case-insensitive). Symlink loops are ignored.
std::vector<FileEntry> list_directory(const std::string& path);

// Path helpers (UTF-8 aware via std::filesystem).
std::string path_join(const std::string& a, const std::string& b);
std::string path_filename(const std::string& path);
std::string path_parent(const std::string& path);
std::string path_extension(const std::string& path);   // includes leading '.', lower-cased
bool path_exists(const std::string& path);
bool is_pxb_file(const std::string& name);

// Stat a file (UTF-8 path) for size + last-modified time, used as a cheap
// pre-check before trusting a cached metadata entry. On failure returns false.
// `mtime_ns` / `size` may be null to skip either field.
bool file_stat(const std::string& path, int64_t* mtime_ns, uint64_t* size);

#ifdef _WIN32
// UTF-8 (std::string) <-> wide (std::wstring / UTF-16) conversions.
// Single source of truth for this conversion across the app, the registry
// helper, and the UI layer — do NOT re-implement it elsewhere (that was a
// "copy-paste then diverge" hazard). Implemented in fileutil.cpp via
// WideCharToMultiByte / MultiByteToWideChar(CP_UTF8).
std::wstring utf8_to_wide(const std::string& u8);
std::string wide_to_utf8(const std::wstring& w);
#endif

// Read an entire file into memory. UTF-8 path on all platforms. On failure
// returns an empty vector and (if `err` is provided) a human-readable reason.
// Used by image::load_png_file and pxb::read_pxb_file.
std::vector<uint8_t> read_file_bytes(const std::string& path, std::string* err = nullptr);

} // namespace pxb
