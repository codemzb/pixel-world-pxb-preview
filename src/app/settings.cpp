// settings.cpp — see settings.h for the storage design.
//
// All file IO goes through UTF-8 -> wide -> Win32 (or plain fopen on other
// platforms): MinGW's narrow CRT would interpret the UTF-8 path as ANSI and
// silently fail on non-ASCII user names (AGENTS §7.2).
//
#include "settings.h"
#include "fileutil.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace pxb {

namespace {

// Directory that contains this exe (UTF-8). Empty on failure / non-Windows.
std::string exe_dir_path() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    UINT n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::wstring w(buf, n);
    size_t slash = w.find_last_of(L"\\/");
    if (slash == std::string::npos) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)slash,
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)slash, s.data(), len,
                        nullptr, nullptr);
    return s;
#else
    return {};
#endif
}

// Per-user config directory (UTF-8): %APPDATA%\PXB Preview on Windows,
// $XDG_CONFIG_HOME/pxb-preview or ~/.config/pxb-preview elsewhere. Empty when
// the environment provides nothing usable.
std::string user_config_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    UINT n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, buf, (int)n,
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, s.data(), len,
                        nullptr, nullptr);
    return path_join(s, "PXB Preview");
#else
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return path_join(xdg, "pxb-preview");
    const char* home = getenv("HOME");
    if (home && *home) return path_join(path_join(home, ".config"), "pxb-preview");
    return {};
#endif
}

std::string exe_side_config_path() {
    std::string d = exe_dir_path();
    return d.empty() ? std::string() : path_join(d, "config.ini");
}

std::string user_config_path() {
    std::string d = user_config_dir();
    return d.empty() ? std::string() : path_join(d, "config.ini");
}

#ifdef _WIN32
// fopen with a UTF-8 path (wide API underneath). `mode` is ASCII.
FILE* open_utf8(const std::string& path, const char* mode) {
    wchar_t wmode[8] = {0};
    for (int i = 0; mode[i] && i < 7; ++i) wmode[i] = (wchar_t)mode[i];
    return _wfopen(utf8_to_wide(path).c_str(), wmode);
}
#else
FILE* open_utf8(const std::string& path, const char* mode) {
    return fopen(path.c_str(), mode);
}
#endif

// Trim ASCII whitespace on both ends.
std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Parse one "key = value" line into `out`; returns false for junk lines.
bool parse_line(const std::string& line, std::string& key, std::string& val) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    key = trim(line.substr(0, eq));
    val = trim(line.substr(eq + 1));
    return !key.empty();
}

bool parse_bool(const std::string& v, bool def) {
    if (v == "1" || v == "true" || v == "on" || v == "yes") return true;
    if (v == "0" || v == "false" || v == "off" || v == "no") return false;
    return def;
}

int parse_int(const std::string& v, int def) {
    return v.empty() ? def : atoi(v.c_str());
}

float parse_float(const std::string& v) { return (float)atof(v.c_str()); }

} // namespace

ViewConfig load_view_config() {
    ViewConfig c;   // defaults
    // Exe-side file wins (portable); per-user file is the fallback.
    const std::string paths[2] = { exe_side_config_path(), user_config_path() };
    for (const std::string& path : paths) {
        if (path.empty() || !path_exists(path)) continue;
        std::string err;
        std::vector<uint8_t> bytes = read_file_bytes(path, &err);
        if (bytes.empty()) continue;
        std::string text(bytes.begin(), bytes.end());
        size_t pos = 0;
        while (pos < text.size()) {
            size_t eol = text.find('\n', pos);
            std::string line = trim(text.substr(pos, eol == std::string::npos
                                                      ? std::string::npos
                                                      : eol - pos));
            pos = (eol == std::string::npos) ? text.size() : eol + 1;
            std::string k, v;
            if (!parse_line(line, k, v)) continue;
            if      (k == "show_list")    c.show_list    = parse_bool(v, c.show_list);
            else if (k == "show_info")    c.show_info    = parse_bool(v, c.show_info);
            else if (k == "show_minimap") c.show_minimap = parse_bool(v, c.show_minimap);
            else if (k == "list_w")       c.list_w = parse_float(v);
            else if (k == "info_w")       c.info_w = parse_float(v);
            else if (k == "map_ox")       c.map_ox = parse_float(v);
            else if (k == "map_oy")       c.map_oy = parse_float(v);
            else if (k == "theme")        c.theme = parse_int(v, c.theme);
        }
        c.valid = true;
        break;
    }
    return c;
}

void save_view_config(const ViewConfig& c) {
    // Prefer the exe directory (portable distribution); if it isn't writable
    // (e.g. an admin install under Program Files), use the per-user dir.
    const std::string exe_path = exe_side_config_path();
    const std::string user_path = user_config_path();
    const std::string* target = &exe_path;
    {
        // Writability probe: appending to an existing file or creating it.
        FILE* probe = exe_path.empty()
                          ? nullptr
                          : open_utf8(exe_path, "ab");
        if (probe) {
            fclose(probe);
        } else {
            // Make sure the per-user directory exists before relying on it.
            if (!user_path.empty()) {
                std::string dir = user_config_dir();
#ifdef _WIN32
                if (!dir.empty()) CreateDirectoryW(utf8_to_wide(dir).c_str(), nullptr);
#else
                // Best effort; mkdir failure (already exists) is fine.
                mkdir(dir.c_str(), 0755);
#endif
            }
            target = &user_path;
        }
    }
    if (target->empty()) return;
    FILE* f = open_utf8(*target, "w");
    if (!f) return;
    fprintf(f, "[view]\n");
    fprintf(f, "show_list=%d\n",    c.show_list ? 1 : 0);
    fprintf(f, "show_info=%d\n",    c.show_info ? 1 : 0);
    fprintf(f, "show_minimap=%d\n", c.show_minimap ? 1 : 0);
    fprintf(f, "list_w=%.1f\n", c.list_w);
    fprintf(f, "info_w=%.1f\n", c.info_w);
    fprintf(f, "map_ox=%.1f\n", c.map_ox);
    fprintf(f, "map_oy=%.1f\n", c.map_oy);
    fprintf(f, "theme=%d\n", c.theme);
    fclose(f);
}

} // namespace pxb
