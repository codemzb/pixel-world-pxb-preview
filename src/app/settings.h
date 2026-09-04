// settings.h
//
// Persisted view configuration (small key=value file, no dependencies).
//
// Storage location is portable-first: next to the exe when that directory is
// writable (zip distribution), otherwise in the per-user config directory
// (%APPDATA%\PXB Preview on Windows, $XDG_CONFIG_HOME/pxb-preview elsewhere).
// Panel sizes are stored in DPI-independent logical units; the UI layer
// converts to/from real pixels with ui_scale() at load/save time.
//
#pragma once

namespace pxb {

struct ViewConfig {
    bool show_list    = false;  // left sibling .pxb list (default: hidden)
    bool show_info    = false;  // right info + layers column (default: hidden)
    bool show_minimap = true;   // bird's-eye overlay in the preview corner
    float list_w = 0.0f;        // panel width, logical units (0 = unset)
    float info_w = 0.0f;
    float map_ox = 0.0f;        // minimap offset from the preview's bottom-right
    float map_oy = 0.0f;
    int theme = 0;              // ThemeMode: 0=System (follow OS), 1=Dark, 2=Light
    bool valid = false;         // true once a config file was actually read
};

// Read the config from disk. Returns defaults (valid=false) when no file is
// found; unknown keys and malformed values are ignored.
ViewConfig load_view_config();

// Write the config. Tries the exe directory first and falls back to the
// per-user directory; silently gives up if neither is writable.
void save_view_config(const ViewConfig& c);

} // namespace pxb
