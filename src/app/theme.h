// theme.h
//
// UI color theme (dark / light / follow-system). The theme's look is fully
// owned by Renderer::apply_theme() (renderer.cpp) — this header only defines
// the user-facing mode and how "System" resolves to an actual appearance on
// each platform, so the renderer and the UI layer (menu, overlay colors)
// always agree on the effective theme.
//
// The choice is persisted in config.ini under the `theme=` key (0=System,
// 1=Dark, 2=Light) — see settings.{h,cpp}. Absent key / unknown value
// defaults to System (follow the OS), which on Windows is read live from
// HKCU's AppsUseLightTheme.
//
#pragma once

namespace pxb {

enum class ThemeMode : int {
    System = 0,   // follow the OS appearance (Windows: AppsUseLightTheme; other platforms: dark)
    Dark   = 1,   // force dark
    Light  = 2,   // force light
};

// Does the OS currently ask for a dark appearance? Best-effort: Windows reads
// HKCU ...\Themes\Personalize\AppsUseLightTheme (cached ~2 s so a theme
// switch is picked up while the app runs); other platforms return true (the
// app's original look). Any failure also falls back to dark.
bool system_prefers_dark();

// Resolve a ThemeMode to the effective appearance.
bool is_dark_theme(ThemeMode mode);

// Clamp a raw int (e.g. from config.ini) to a valid ThemeMode.
ThemeMode theme_mode_from_int(int v);

} // namespace pxb