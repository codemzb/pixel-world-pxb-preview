// theme.cpp — see theme.h for the design.
//
#include "theme.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <ctime>

namespace pxb {

namespace {
// The registry query is cheap, but System mode re-resolves every frame, so
// cache the result briefly. A live OS theme flip then applies within ~2 s
// without a restart, and steady-state costs nothing.
bool g_system_dark = true;
std::time_t g_cached_at = 0;
constexpr std::time_t kCacheSeconds = 2;
} // namespace

bool system_prefers_dark() {
    const std::time_t now = std::time(nullptr);
    if (now - g_cached_at < kCacheSeconds) return g_system_dark;
    g_cached_at = now;
    bool dark = true;   // unknown → keep the app's original dark look
#ifdef _WIN32
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD v = 1, size = sizeof(v);
        if (RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&v), &size) == ERROR_SUCCESS &&
            size == sizeof(v)) {
            dark = (v == 0);   // AppsUseLightTheme=1 → light apps
        }
        RegCloseKey(key);
    }
#endif
    g_system_dark = dark;
    return dark;
}

bool is_dark_theme(ThemeMode mode) {
    if (mode == ThemeMode::Light) return false;
    if (mode == ThemeMode::Dark) return true;
    return system_prefers_dark();
}

ThemeMode theme_mode_from_int(int v) {
    if (v >= (int)ThemeMode::Dark && v <= (int)ThemeMode::Light)
        return (ThemeMode)v;
    return ThemeMode::System;
}

} // namespace pxb