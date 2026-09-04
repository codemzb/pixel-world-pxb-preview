// ui.cpp
//
// Compact previewer UI (three columns + optional bottom strip):
//   +----------+----------------------+-----------+
//   | pxb文件  | pxb预览               | pxb信息   |
//   | (scroll) |  (wheel = zoom,      | (narrow)  |
//   |  w/o bar |   drag = pan,        +-----------+
//   |          |   no scrollbar)      | 图层      |
//   |          |   ▣ minimap ↘        |  ☑ 图层1  |
//   +----------+----------------------+-----------+
//   | 帧  (only when multi-frame)                |
//   |  [transport] [帧率] + filmstrip            |
//   +--------------------------------------------+
//
// Layout is user-configurable: the View (视图) menu toggles the left file
// list, the right info+layers column and the preview's bird's-eye minimap;
// the two side panels are resized by dragging the splitter strips between
// them and the preview (widths persist in App for the session).
//
// No container ever shows a scrollbar: the pxb list and filmstrip scroll via
// the mouse wheel; the preview zooms with the wheel and pans with left-drag
// (pan is clamped so the image never reveals empty space past its edges).
//
// Keyboard: ←/→ open the previous/next sibling .pxb (follows the search
// filter); Esc closes the current window (About first, else the app).
//
// Texture filter is GL_NEAREST (set in Renderer::texture_for), so the image
// stays crisp at any zoom — no bilinear blur.
//
#include "ui.h"
#include "app.h"
#include "renderer.h"
#include "fileutil.h"
#include "regutil.h"
#include "i18n.h"
#include "image.h"
#include "settings.h"
#include "theme.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

#include <SDL3/SDL.h>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <memory>

// Resource IDs — must match assets/icon.rc (single source: the .rc file).
#ifndef IDD_ABOUT
#define IDI_APP           1
#define IDD_ABOUT         100
#define IDC_STATIC_TITLE  1001
#define IDC_STATIC_VER    1002
#define IDC_STATIC_BODY   1003
#define IDC_BTN_WEBSITE   1004
#endif
#endif

namespace pxb {

// ---- Shared UI layout constants ----------------------------------------------
// Hoisted out of the per-frame draw code so the three-column + bottom-strip
// geometry can be tuned in one place (see CODE_REVIEW P4: layout magic
// numbers). Internal linkage (static) keeps them file-local.
static constexpr float kListWidth    = 200.0f;  // left pxb-file list width
static constexpr float kInfoWidth    = 190.0f;  // right pxb信息 width (narrowed)
static constexpr float kFramesHeight = 130.0f;  // 帧 panel height (title + transport + filmstrip)
static constexpr float kPanelGap     = 4.0f;    // gap between panels
static constexpr float kStatusHeight = 18.0f;   // always-visible status bar height

// UI-side DPI scale factor. Fonts are rasterised at (base_size * dpi_scale),
// so current font height / base recovers the scale inside UI code — including
// helpers that don't receive the Renderer. ImGui's ScaleAllSizes only covers
// style metrics; every hand-set layout constant above (and the scattered
// pixel offsets below) is authored at 100% DPI and must be multiplied by
// this at the use site, or panels stay 100%-sized on a 175% display.
static float ui_scale() { return ImGui::GetFontSize() / 16.0f; }

// ---- About: native OS dialog (Windows) --------------------------------------
// A real, independent top-level window (dialog resource IDD_ABOUT in
// assets/icon.rc), NOT an ImGui floating window. On non-Windows platforms the
// UI falls back to an ImGui window (app.show_about).
#ifdef _WIN32
// Thin const-char* wrapper over the shared pxb::utf8_to_wide (single source in
// fileutil.cpp). Kept so the many call sites below don't have to be rewritten.
static std::wstring u8w(const char* s) {
    return pxb::utf8_to_wide(s ? std::string(s) : std::string());
}

static INT_PTR CALLBACK about_dlg_proc(HWND hDlg, UINT msg, WPARAM wParam,
                                       LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        // Reuse the exe's embedded Pixel World icon (resource ID 1) for the
        // dialog's title bar and taskbar entry.
        HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP));
        if (icon) {
            SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)icon);
            SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)icon);
        }
        // Localize at runtime (the resource template ships in English).
        char line[256];
        snprintf(line, sizeof(line), tr(Str::AboutVersionFmt), kAppVersion);
        std::wstring ver = u8w(line);
        snprintf(line, sizeof(line), tr(Str::AboutBuildFmt), __DATE__);
        std::wstring build = u8w(line);
        // Compact rows (no labels — keeps the dialog small):
        //   SDL3 + Dear ImGui + OpenGL 2.1
        //   Built Aug 21 2026
        //
        //   https://px.mzb.one
        //   Copyright (c) 2026 https://px.mzb.one
        //   MZB · 暂未确定（计划开源）
        std::wstring body;
        body += u8w(tr(Str::AboutPowered));
        body += L"\n" + build;
        body += L"\n\n" + u8w(kHomepage);
        body += L"\n" + u8w(kAppCopyright);
        body += L"\n" + u8w(kAppAuthor) + L" · " + u8w(tr(Str::LicenseValue));

        SetWindowTextW(hDlg, u8w(tr(Str::AboutTitle)).c_str());
        SetDlgItemTextW(hDlg, IDC_STATIC_TITLE, u8w(kAppName).c_str());
        SetDlgItemTextW(hDlg, IDC_STATIC_VER, ver.c_str());
        SetDlgItemTextW(hDlg, IDC_STATIC_BODY, body.c_str());
        SetDlgItemTextW(hDlg, IDC_BTN_WEBSITE, u8w(tr(Str::AboutOpenHomepage)).c_str());
        SetDlgItemTextW(hDlg, IDOK, u8w(tr(Str::AboutClose)).c_str());
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_BTN_WEBSITE) {
            ShellExecuteW(nullptr, L"open", u8w(kHomepage).c_str(), nullptr,
                          nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// Show the About dialog as a modal, OS-owned window above the SDL window.
static void show_about_dialog(void* sdl_window) {
    HWND parent = nullptr;
    if (sdl_window) {
        parent = (HWND)SDL_GetPointerProperty(
            SDL_GetWindowProperties((SDL_Window*)sdl_window),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    }
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ABOUT),
                    parent, about_dlg_proc, 0);
}
#endif // _WIN32

// Locate the directory that contains this exe (UTF-8). Returns "" on failure
// or on non-Windows platforms (callers should fall back to cwd-relative).
static std::string this_exe_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::wstring w(buf, n);
    size_t slash = w.find_last_of(L"\\/");
    if (slash == std::string::npos) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)slash, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)slash, s.data(), len, nullptr, nullptr);
    return s;
#else
    return {};
#endif
}

// Load icon.png (next to the exe, falling back to cwd) and downscale to
// 128x128 for the About window. The .ico embedded into the exe already
// covers the title bar / taskbar; this is the in-app preview glyph. The
// file is loaded once and cached statically.
static const RgbaImage& about_icon_image() {
    static RgbaImage img;
    static bool loaded = false;
    if (loaded) return img;
    loaded = true;
    // Try exe-dir first (handles double-click / file-association launches
    // where cwd is not the install folder), then fall back to cwd.
    std::string d = this_exe_dir();
    if (!d.empty()) img = load_png_file(path_join(d, "icon.png"));
    if (img.empty()) img = load_png_file("icon.png");
    if (img.empty()) return img;
    // Downscale to 128x128 via the shared nearest-neighbour helper in
    // core/image.cpp (the thumbnail integrations reuse the same routine).
    // At this size NN is visually fine and keeps image math out of the UI.
    const int N = 128;
    img = resize_nearest(img, N);
    return img;
}

// ---- .pxb file-association flow (Windows) ----------------------------------
// The double-click "open" verb lives under HKCU and never needs elevation, so
// we first try the operation in-process (works for the current user without
// UAC). Only if the user explicitly wants machine-wide thumbnail registration
// is elevation relevant — but that is best-effort and non-fatal. We therefore
// no longer force a UAC prompt on every register/unregister; the in-process
// call handles the common case, and any thumbnail note is surfaced as a
// non-blocking message instead of an error.
static void run_association(bool do_register) {
#ifdef _WIN32
    std::string err;
    const bool ok = do_register ? register_pxb_association(err)
                                : unregister_pxb_association(err);
    const char* title = do_register ? tr(Str::AssocRegisterTitle)
                                    : tr(Str::AssocUnregisterTitle);
    if (ok) {
        // err may carry a non-fatal note (e.g. thumbnails skipped).
        if (err.empty()) {
            const char* msg = do_register ? tr(Str::AssocRegisterOk)
                                          : tr(Str::AssocUnregisterOk);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, msg, nullptr);
        } else {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title,
                                     err.c_str(), nullptr);
        }
    } else {
        std::string msg = tr(Str::AssocFailPrefix);
        msg += err;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg.c_str(), nullptr);
    }
#endif
}

// ---- native open-file dialog (Windows) --------------------------------------
static std::string open_file_dialog() {
#ifdef _WIN32
    wchar_t file[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    // Win32 filter is a double-NUL-terminated sequence of "Name\0Pattern\0"
    // pairs. Build it with explicit push_back of L'\0' — appending string
    // literals like L"\0*.pxb\0" via operator+= truncates at the first NUL
    // (the literal is only L""), which silently broke the filter and made the
    // dialog show no .pxb files.
    std::wstring filter;
    filter += trw(Str::DlgPxbFiles); filter += L'\0';
    filter += L"*.pxb";              filter += L'\0';
    filter += trw(Str::DlgAllFiles); filter += L'\0';
    filter += L"*.*";                filter += L'\0';
    filter += L'\0';                       // terminate the whole list
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, file, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string utf8(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, file, -1, &utf8[0], len, nullptr, nullptr);
    return utf8;
#else
    return {};
#endif
}

// ---- sibling list filter (shared by the list UI and ←/→ navigation) ---------
// Indices into app.sibling_pxb() matching the search box (case-insensitive,
// by filename). Empty filter = all siblings. Returned by reference: the
// vector is rebuilt once per frame and reused across call sites.
static const std::vector<size_t>& filtered_siblings(const App& app) {
    static std::vector<size_t> filtered;
    filtered.clear();
    const auto& sibs = app.sibling_pxb();
    std::string filter;
    for (const char* f = app.sibling_filter; *f; ++f)
        filter += (char)std::tolower((unsigned char)*f);
    for (size_t i = 0; i < sibs.size(); ++i) {
        if (!filter.empty()) {
            std::string name = path_filename(sibs[i]);
            for (char& c : name) c = (char)std::tolower((unsigned char)c);
            if (name.find(filter) == std::string::npos) continue;
        }
        filtered.push_back(i);
    }
    return filtered;
}

// ---- file-list row helpers -------------------------------------------------
// Number of wrapped lines `t` occupies at `wrap_w` with the current font
// (floor of the measured height; empty text counts as one line).
static int wrapped_lines(const std::string& t, float wrap_w, float line_h) {
    if (t.empty()) return 1;
    ImVec2 sz = ImGui::CalcTextSize(t.c_str(), nullptr, false, wrap_w);
    return std::max(1, (int)ceilf(sz.y / line_h - 0.01f));
}

// UTF-8 helpers: filenames are clamped on code-point boundaries so a CJK
// name never gets split mid-character (plain byte trimming would corrupt it).
static size_t utf8_seq_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    return 4;   // 4-byte sequences (0xF0 lead)
}
static size_t utf8_cp_count(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size();) {
        i += utf8_seq_len((unsigned char)s[i]);
        ++n;
    }
    return n;
}
static size_t utf8_byte_after(const std::string& s, size_t cps) {
    size_t i = 0;
    for (size_t k = 0; k < cps && i < s.size(); ++k)
        i += utf8_seq_len((unsigned char)s[i]);
    return i;
}

// Clamp `name` to at most `max_lines` wrapped lines within `wrap_w`; the last
// visible line ends with "…" when it was truncated. Binary search over code
// points keeps the whole operation a handful of cheap text measurements.
static std::string clamp_wrapped(const std::string& name, float wrap_w,
                                 float line_h, int max_lines) {
    if (wrapped_lines(name, wrap_w, line_h) <= max_lines) return name;
    const std::string ell = "\xE2\x80\xA6";   // U+2026 HORIZONTAL ELLIPSIS (in the atlas)
    const size_t n = utf8_cp_count(name);
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        std::string p = name.substr(0, utf8_byte_after(name, mid)) + ell;
        if (wrapped_lines(p, wrap_w, line_h) <= max_lines) lo = mid;
        else hi = mid - 1;
    }
    return name.substr(0, utf8_byte_after(name, lo)) + ell;
}

// Human-readable byte counts ("1.2 MB"); the list meta line never needs more
// precision than one decimal.
static std::string format_bytes(uint64_t b) {
    char buf[32];
    if (b < 1024)
        snprintf(buf, sizeof buf, "%llu B", (unsigned long long)b);
    else if (b < 1024ull * 1024)
        snprintf(buf, sizeof buf, "%.1f KB", (double)b / 1024.0);
    else if (b < 1024ull * 1024 * 1024)
        snprintf(buf, sizeof buf, "%.1f MB", (double)b / (1024.0 * 1024.0));
    else
        snprintf(buf, sizeof buf, "%.2f GB", (double)b / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

// Local "YYYY-MM-DD HH:mm" for a unix timestamp; empty on conversion failure.
static std::string format_mtime(int64_t unix_sec) {
    time_t t = (time_t)unix_sec;
    struct tm tm;
#ifdef _WIN32
    if (localtime_s(&tm, &t) != 0) return {};
#else
    if (!localtime_r(&t, &tm)) return {};
#endif
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

// Left/Right arrow keys: open the previous/next sibling .pxb (wraps around).
// Follows the filtered list, so ←/→ steps through what the search box shows.
// The current file is matched by FILENAME: the OS launch path may use
// different separators than the sibling list entries (backslash vs /), and a
// byte-for-byte path compare would silently miss it.
static void navigate_siblings(App& app, int dir) {
    const std::vector<size_t>& fl = filtered_siblings(app);
    if (fl.empty()) return;
    // When the current file isn't in the (filtered) list, treat it as sitting
    // just before/after it, so the first ←/→ enters the list from that side.
    size_t idx = (dir > 0) ? fl.size() - 1 : 0;
    bool found = false;
    const std::string cur_name = path_filename(app.current_path);
    for (size_t i = 0; i < fl.size(); ++i) {
        if (path_filename(app.sibling_pxb()[fl[i]]) == cur_name) {
            idx = i; found = true; break;
        }
    }
    if (found) idx = (idx + dir + fl.size()) % fl.size();
    const std::string p = app.sibling_pxb()[fl[idx]];
    if (path_filename(p) != cur_name) app.load_file(p);
}

// ---- center: mouse-wheel-zoomable image view (no scrollbar) ------------------
// Pick the image the preview (and minimap) shows. While a layer WITH an
// isolated preview is hidden, the blend of the visible layers is displayed
// instead of the composited frame / thumbnail (the per-layer renders are
// 128x128 downscales, so the composite is lower-res — used only while
// filtering). Returns nullptr when there is nothing to show; fills the
// texture-cache key and the composite zoom scale (1.0 for full-res images).
static const RgbaImage* pick_display_image(App& app, int* key, float* comp_scale) {
    *key = App::kDocThumbTexKey;   // dedicated key for the thumbnail fallback
    *comp_scale = 1.0f;
    if (const RgbaImage* comp = app.layer_composite_if_filtering()) {
        *key = App::kLayerCompositeTexKey;
        *comp_scale = app.layer_composite_scale();
        return comp;
    }
    if (!app.doc.frame_images.empty() && app.current_frame < app.frame_count()) {
        *key = app.current_frame;
        return &app.doc.frame_images[app.current_frame].image;
    }
    if (!app.doc.thumbnail.empty())
        return &app.doc.thumbnail;   // files with only a thumbnail (no frames)
    return nullptr;
}

// ---- minimap (bird's-eye overview) --------------------------------------------
// Small overlay in the preview's bottom-right corner: the whole image plus a
// rectangle marking the region currently visible in the preview.
//   - drag the header strip to move the overlay inside the preview area
//   - click / drag inside the map to move the preview's visible region
// Position is stored in App as an offset from the preview's bottom-right
// corner (survives window resizes); pan writes use the same preview-pixel
// convention as draw_image_view, so both stay consistent.
static void draw_minimap(App& app, Renderer& renderer, const RgbaImage& img,
                         int tex_key, const ImVec2& avail, const ImVec2& pos,
                         const ImVec2& disp, float zeff) {
    const float sc = ui_scale();
    const float map_w  = 150.0f * sc;
    const float map_h  = 110.0f * sc;
    const float pad    = 5.0f * sc;
    const float head_h = ImGui::GetTextLineHeight() + 3.0f * sc;
    const float total_w = map_w + pad * 2.0f;
    const float total_h = 2.0f * sc + head_h + 2.0f * sc + map_h + 3.0f * sc;
    // Skip when the preview is too small to contain the overlay.
    if (avail.x < total_w + 8.0f * sc || avail.y < total_h + 8.0f * sc) return;

    // Offset from the bottom-right corner; <=0 → first use → default margin.
    const float m = 8.0f * sc;
    auto clamp_off = [m](float v, float total, float space) {
        return std::clamp(v, m, std::max(m, space - total - m));
    };
    if (app.minimap_off_x <= 0.0f) app.minimap_off_x = m;
    if (app.minimap_off_y <= 0.0f) app.minimap_off_y = m;
    app.minimap_off_x = clamp_off(app.minimap_off_x, total_w, avail.x);
    app.minimap_off_y = clamp_off(app.minimap_off_y, total_h, avail.y);

    // Translucent overlay chrome must contrast with the canvas in BOTH themes
    // (dark-mode value is the original look; light-mode is its inverse).
    const bool dark = is_dark_theme(app.theme_mode);
    ImGui::SetCursorPos(ImVec2(avail.x - total_w - app.minimap_off_x,
                               avail.y - total_h - app.minimap_off_y));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
        dark ? ImVec4(0.07f, 0.07f, 0.09f, 0.85f)
             : ImVec4(0.99f, 0.99f, 1.00f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border,
        dark ? ImVec4(0.38f, 0.38f, 0.45f, 0.70f)
             : ImVec4(0.52f, 0.52f, 0.60f, 0.80f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, 2.0f * sc));
    ImGui::BeginChild("##minimap", ImVec2(total_w, total_h), true,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse |
                      ImGuiWindowFlags_NoSavedSettings);

    // Header: full-width drag strip with the label drawn on top of it.
    ImGui::InvisibleButton("##mm_drag", ImVec2(map_w, head_h));
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    if (ImGui::IsItemActive()) {
        app.minimap_off_x -= ImGui::GetIO().MouseDelta.x;
        app.minimap_off_y -= ImGui::GetIO().MouseDelta.y;
        app.minimap_off_x = clamp_off(app.minimap_off_x, total_w, avail.x);
        app.minimap_off_y = clamp_off(app.minimap_off_y, total_h, avail.y);
    }
    {
        const ImVec2 dm = ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(dm.x, dm.y + (head_h - ImGui::GetTextLineHeight()) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_Text, 0.80f), tr(Str::PanelMinimap));
    }

    // The map: whole image contained in map_w x map_h (opaque black backdrop
    // so transparent pxb pixels read against the translucent overlay).
    const float k = std::min(map_w / (float)img.width, map_h / (float)img.height);
    const ImVec2 msz((float)img.width * k, (float)img.height * k);
    const ImVec2 map_pos(pad + (map_w - msz.x) * 0.5f,
                         2.0f * sc + head_h + (map_h - msz.y) * 0.5f);
    void* tex = renderer.texture_for(img, tex_key);
    ImGui::SetCursorPos(map_pos);
    ImGui::Image(tex, msz, ImVec2(0, 0), ImVec2(1, 1),
                 ImVec4(1, 1, 1, 1), ImVec4(0, 0, 0, 1));
    // A plain Image is display-only: it never becomes "active", so click/drag
    // must be captured by an InvisibleButton laid over the same rect.
    ImGui::SetCursorPos(map_pos);
    ImGui::InvisibleButton("##mm_area", msz);
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const ImVec2 imn = ImGui::GetItemRectMin();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Visible (focused) region: preview pixels → image space (divide by the
    // effective zoom, origin at -pos) → map space (multiply by k), clamped to
    // the map.
    const float vx0 = -pos.x / zeff,             vy0 = -pos.y / zeff;
    const float vx1 = (avail.x - pos.x) / zeff,  vy1 = (avail.y - pos.y) / zeff;
    const ImVec2 map_end(imn.x + msz.x, imn.y + msz.y);
    ImVec2 r0(std::clamp(imn.x + vx0 * k, imn.x, map_end.x),
              std::clamp(imn.y + vy0 * k, imn.y, map_end.y));
    ImVec2 r1(std::clamp(imn.x + vx1 * k, imn.x, map_end.x),
              std::clamp(imn.y + vy1 * k, imn.y, map_end.y));

    // Focus box: dim everything OUTSIDE the visible region so the box pops,
    // then stroke it with a high-contrast dark outline + a bright warm line.
    if (r1.x > r0.x && r1.y > r0.y) {
        const ImU32 dim = IM_COL32(0, 0, 0, 135);
        if (r0.x > imn.x) dl->AddRectFilled(ImVec2(imn.x, imn.y),
                                            ImVec2(r0.x, map_end.y), dim);
        if (r1.x < map_end.x) dl->AddRectFilled(ImVec2(r1.x, imn.y),
                                                ImVec2(map_end.x, map_end.y), dim);
        if (r0.y > imn.y) dl->AddRectFilled(ImVec2(r0.x, imn.y),
                                            ImVec2(r1.x, r0.y), dim);
        if (r1.y < map_end.y) dl->AddRectFilled(ImVec2(r0.x, r1.y),
                                                ImVec2(r1.x, map_end.y), dim);
        dl->AddRect(r0, r1, IM_COL32(0, 0, 0, 220), 2.0f * sc, 0, 3.0f * sc);
        dl->AddRect(r0, r1, IM_COL32(255, 214, 100, 255), 2.0f * sc, 0, 1.5f * sc);
    }

    // Drag the box itself: grab it anywhere and it follows the cursor with a
    // fixed anchor offset (so it never jumps away from the press point), then
    // the preview view is set to keep the dragged region on screen. Pressing
    // on empty map area instead recenters the view on that spot.
    auto clamp_pan = [](float& p, float d, float a) {
        p = (d > a) ? std::clamp(p, a - d, 0.0f) : 0.0f;
    };
    static ImVec2 s_grab_off;
    if (ImGui::IsItemActivated()) {
        const ImVec2 mid((r0.x + r1.x) * 0.5f, (r0.y + r1.y) * 0.5f);
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const bool over_box = mp.x >= r0.x && mp.x <= r1.x &&
                              mp.y >= r0.y && mp.y <= r1.y;
        // Over the box: keep the grab point fixed under the cursor; elsewhere:
        // recenter the box on the press point.
        s_grab_off = over_box ? ImVec2(mp.x - mid.x, mp.y - mid.y)
                              : ImVec2(0.0f, 0.0f);
    }
    if (ImGui::IsItemActive()) {
        const ImVec2 mid(ImGui::GetIO().MousePos.x - s_grab_off.x,
                         ImGui::GetIO().MousePos.y - s_grab_off.y);
        const ImVec2 ic((mid.x - imn.x) / k, (mid.y - imn.y) / k);
        app.pan_x = avail.x * 0.5f - ic.x * zeff;
        app.pan_y = avail.y * 0.5f - ic.y * zeff;
        clamp_pan(app.pan_x, disp.x, avail.x);
        clamp_pan(app.pan_y, disp.y, avail.y);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

// ---- draggable splitters ------------------------------------------------------
// Thin full-height strip between two side panels: drag to resize the panel
// before it. Renders a subtle grab line; cursor becomes ResizeEW when
// hovered. Returns the mouse dx while dragged (0 otherwise).
static float panel_splitter(const char* id, float height) {
    const float sc = ui_scale();
    const float w = 6.0f * sc;
    ImGui::SameLine(0.0f, 0.0f);
    const ImVec2 line_pos = ImGui::GetCursorPos();   // top of the current row
    ImGui::InvisibleButton(id, ImVec2(w, height));
    const bool hov = ImGui::IsItemHovered();
    const bool act = ImGui::IsItemActive();
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    const float cx = (mn.x + mx.x) * 0.5f;
    const ImU32 col = act ? ImGui::GetColorU32(ImGuiCol_SliderGrabActive)
                          : hov ? ImGui::GetColorU32(ImGuiCol_SliderGrab)
                                : ImGui::GetColorU32(ImGuiCol_Separator);
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(cx - 1.0f, mn.y + 2.0f * sc),
        ImVec2(cx + 1.0f, mx.y - 2.0f * sc), col);
    // A full-height item makes ImGui's auto cursor wrap to the next line,
    // which pushed the following child below the clip rect (invisible row).
    // Pin the cursor back to the top of this row, just after the strip.
    ImGui::SetCursorPos(ImVec2(line_pos.x + w, line_pos.y));
    return act ? ImGui::GetIO().MouseDelta.x : 0.0f;
}

// ---- center: mouse-wheel-zoomable image view (no scrollbar) ------------------
static void draw_image_view(App& app, Renderer& renderer) {
    // Draw directly into the parent "preview" child (which supplies the border
    // and title). No scrollbar ever appears: the wheel zooms (below), and the
    // image is centered / clipped, so there is nothing to scroll.
    ImVec2 avail = ImGui::GetContentRegionAvail();
    // Screen-space top-left of the image region (the area below the panel
    // title). All zoom anchoring is computed against this origin.
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    // One-shot auto-fit when a new document is loaded: fill the preview area
    // without clipping, so small pixel art doesn't sit as a tiny island.
    if (app.fit_next_frame && app.has_document()) {
        const RgbaImage* img = nullptr;
        if (!app.doc.frame_images.empty() && app.current_frame < app.frame_count())
            img = &app.doc.frame_images[app.current_frame].image;
        else if (!app.doc.thumbnail.empty())
            img = &app.doc.thumbnail;
        if (img && !img->empty() && img->width > 0 && img->height > 0) {
            float sx = avail.x / (float)img->width;
            float sy = avail.y / (float)img->height;
            float z = std::min(sx, sy);
            if (z < App::kZoomMin) z = App::kZoomMin;
            if (z > App::kZoomMax) z = App::kZoomMax;
            app.zoom = z;
        }
        app.fit_next_frame = false;
    }

    // Capture the wheel here; the zoom itself is applied further down, once
    // the image geometry is known, so it can anchor at the mouse position.
    float wheel = 0.0f;
    if (ImGui::IsWindowHovered()) wheel = ImGui::GetIO().MouseWheel;

    if (!app.has_document()) {
        // Centered empty-state block: each line is measured and centered, and
        // the block as a whole is vertically centered. The old version used
        // three hard-coded offsets that matched neither the line widths nor
        // the DPI scale (the "staircase" look).
        const char* lines[3] = { tr(Str::EmptyNoFile1), tr(Str::EmptyNoFile2),
                                 tr(Str::EmptyNoFile3) };
        const float lh = ImGui::GetTextLineHeight();
        const float step = lh + ImGui::GetStyle().ItemSpacing.y;
        const float block_h = 3.0f * lh + 2.0f * ImGui::GetStyle().ItemSpacing.y;
        float y = (avail.y - block_h) * 0.5f;
        for (const char* line : lines) {
            const float x = (avail.x - ImGui::CalcTextSize(line).x) * 0.5f;
            ImGui::SetCursorPos(ImVec2(x, y));
            ImGui::TextDisabled("%s", line);
            y += step;
        }
        return;
    }

    // Pick the source image for the current frame (see pick_display_image):
    // current frame, else thumbnail; while a layer WITH an isolated preview is
    // hidden, the lower-resolution visible-layer composite instead.
    int key = App::kDocThumbTexKey;
    float comp_scale = 1.0f;
    const RgbaImage* img = pick_display_image(app, &key, &comp_scale);
    if (!img || img->empty()) {
        ImGui::TextDisabled("%s", tr(Str::EmptyNoPreview));
        return;
    }

    // Apply zoom `z2eff` so that the image-space point `c` (expressed at the
    // CURRENT effective zoom) stays under viewport point `vp` (pixels from
    // `origin`). Axes where the zoomed image fits re-center; the others clamp
    // pan with the same no-empty-space rule as drag-pan.
    auto apply_zoom = [&](float z2eff, const ImVec2& c, const ImVec2& vp) {
        const float zmin = App::kZoomMin * comp_scale;
        const float zmax = App::kZoomMax * comp_scale;
        if (z2eff < zmin) z2eff = zmin;
        if (z2eff > zmax) z2eff = zmax;
        const float dx = (float)img->width * z2eff;
        const float dy = (float)img->height * z2eff;
        const float px = vp.x - c.x * z2eff;
        const float py = vp.y - c.y * z2eff;
        app.pan_x = (dx <= avail.x) ? (avail.x - dx) * 0.5f
                                    : std::clamp(px, avail.x - dx, 0.0f);
        app.pan_y = (dy <= avail.y) ? (avail.y - dy) * 0.5f
                                    : std::clamp(py, avail.y - dy, 0.0f);
        app.zoom = z2eff / comp_scale;
    };

    float zcur = app.zoom * comp_scale;
    void* tex = renderer.texture_for(*img, key);
    ImVec2 disp((float)img->width * zcur, (float)img->height * zcur);

    // --- drag-to-pan (only effective when the image is larger than view) ---
    bool hovered = ImGui::IsWindowHovered();
    bool dragging = hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f);
    if (dragging) {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        app.pan_x += d.x;
        app.pan_y += d.y;
    }
    if (hovered) {
        ImGui::SetMouseCursor(dragging ? ImGuiMouseCursor_ResizeAll
                                       : ImGuiMouseCursor_Hand);
    }
    // Clamp pan so the image always fills / spans the viewport — never leaving
    // empty space beyond its own edges (no out-of-bounds panning).
    if (disp.x > avail.x)
        app.pan_x = std::clamp(app.pan_x, avail.x - disp.x, 0.0f);
    else
        app.pan_x = 0.0f;
    if (disp.y > avail.y)
        app.pan_y = std::clamp(app.pan_y, avail.y - disp.y, 0.0f);
    else
        app.pan_y = 0.0f;

    // Image origin at the CURRENT zoom (needed to find the content point the
    // zoom should anchor on). Same rules as the final position below.
    ImVec2 pos1;
    pos1.x = (disp.x <= avail.x) ? (avail.x - disp.x) * 0.5f : app.pan_x;
    pos1.y = (disp.y <= avail.y) ? (avail.y - disp.y) * 0.5f : app.pan_y;

    // Mouse wheel = zoom anchored at the cursor (1.1x per notch): the image
    // point under the mouse stays under the mouse across the zoom.
    if (wheel != 0.0f) {
        const float factor = (wheel > 0.0f) ? 1.1f : (1.0f / 1.1f);
        const ImVec2 vp(ImGui::GetIO().MousePos.x - origin.x,
                        ImGui::GetIO().MousePos.y - origin.y);
        const ImVec2 c((vp.x - pos1.x) / zcur, (vp.y - pos1.y) / zcur);
        apply_zoom(zcur * factor, c, vp);
        zcur = app.zoom * comp_scale;
        disp = ImVec2((float)img->width * zcur, (float)img->height * zcur);
    }

    // Base position: centered when the image fits, panned (within clamp) when
    // it is larger than the preview area.
    ImVec2 pos;
    pos.x = (disp.x <= avail.x) ? (avail.x - disp.x) * 0.5f : app.pan_x;
    pos.y = (disp.y <= avail.y) ? (avail.y - disp.y) * 0.5f : app.pan_y;

    ImVec2 cur = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(cur.x + pos.x, cur.y + pos.y));
    ImGui::Image(tex, disp);
    ImGui::SetCursorPos(cur);

    // Zoom badge (top-left). Text color follows the theme: on the light theme
    // the badge often floats over the (light) canvas, not the image.
    char zoom_label[32];
    snprintf(zoom_label, sizeof(zoom_label), "%.0f%%", app.zoom * 100.0f);
    const float sc = ui_scale();
    const bool dark = is_dark_theme(app.theme_mode);
    const ImVec4 badge_col = dark ? ImVec4(0.95f, 0.95f, 0.95f, 0.9f)
                                  : ImVec4(0.12f, 0.12f, 0.15f, 0.88f);
    ImGui::SetCursorPos(ImVec2(cur.x + 8.0f * sc, cur.y + 8.0f * sc));
    ImGui::TextColored(badge_col, "%s", zoom_label);
    ImGui::SetCursorPos(cur);

    // Frame counter overlay (only when animating).
    if (app.frame_count() > 1) {
        char badge[32];
        snprintf(badge, sizeof(badge), "%d / %d",
                 app.current_frame + 1, app.frame_count());
        ImGui::SetCursorPos(ImVec2(cur.x + 8.0f * sc,
                                   cur.y + 8.0f * sc + ImGui::GetTextLineHeight() + 4.0f * sc));
        ImGui::TextColored(badge_col, "%s", badge);
        ImGui::SetCursorPos(cur);
    }

    // Zoom controls (absolute, top-right corner — same positioning approach as
    // before, just moved from bottom-right and restyled: compact, rounded,
    // translucent so they don't fight the image for attention).
    if (app.has_document()) {
        const float sc = ui_scale();
        const float pad = 6.0f * sc;   // inner padding
        const float gap = 3.0f * sc;   // between buttons
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pad, 3.0f * sc));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f * sc);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 0));
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.0f, 0.0f, 0.40f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.42f, 0.92f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.22f, 0.32f, 0.78f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.95f, 0.95f, 0.95f, 1.0f));

        // Layout right-to-left so "+" sits at the far right: [适应] [1:1] [-] [+]
        const char* labels[] = { tr(Str::ZoomFit), "1:1", "-", "+" };
        float widths[4];
        float total = 0.0f;
        for (int i = 0; i < 4; i++) {
            widths[i] = ImGui::CalcTextSize(labels[i]).x + pad * 2.0f;
            total += widths[i];
        }
        total += gap * 3.0f;
        float x = avail.x - total - 8.0f * sc;
        const float y = 8.0f * sc;
        ImGui::SetCursorPos(ImVec2(x, y));

        // Button zooms anchor at the viewport center: the content point
        // currently at the center stays there (mouse wheel anchors at the
        // cursor instead — see apply_zoom above).
        auto zoom_centered = [&](float z2eff) {
            const float z = app.zoom * comp_scale;
            const ImVec2 vp(avail.x * 0.5f, avail.y * 0.5f);
            const ImVec2 c((vp.x - pos.x) / z, (vp.y - pos.y) / z);
            apply_zoom(z2eff, c, vp);
        };

        // Fit
        if (ImGui::Button(labels[0])) {
            float sx = avail.x / (float)img->width;
            float sy = avail.y / (float)img->height;
            zoom_centered(std::min(sx, sy));
        }
        ImGui::SameLine();
        if (ImGui::Button(labels[1])) zoom_centered(comp_scale);   // 1:1
        ImGui::SameLine();
        if (ImGui::Button(labels[2])) zoom_centered(app.zoom * comp_scale / 1.25f);
        ImGui::SameLine();
        if (ImGui::Button(labels[3])) zoom_centered(app.zoom * comp_scale * 1.25f);

        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(3);
        ImGui::SetCursorPos(cur);
    }

    // Bird's-eye minimap overlay (bottom-right; draggable, click to pan).
    // Uses the final image position/zoom computed above so the viewport
    // rectangle always matches what is actually on screen.
    if (app.show_minimap)
        draw_minimap(app, renderer, *img, key, avail, pos, disp, zcur);
}

// ---- playback bar -----------------------------------------------------------
static void draw_playback(App& app) {
    const int n = app.frame_count();
    const bool has = n > 0;

    if (!has) ImGui::BeginDisabled();

    if (ImGui::Button(app.playing ? tr(Str::Pause) : tr(Str::Play))) {
        app.playing = !app.playing;
        app.play_accum = 0.0;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::Stop))) {
        app.stop_playback();
    }
    ImGui::SameLine();
    if (ImGui::Button("|<<")) { app.current_frame = 0; }
    ImGui::SameLine();
    if (ImGui::Button("<"))  { app.current_frame = (app.current_frame + n - 1) % n; }
    ImGui::SameLine();
    if (ImGui::Button(">"))  { app.current_frame = (app.current_frame + 1) % n; }
    ImGui::SameLine();
    if (ImGui::Button(">|")) { app.current_frame = n - 1; }

    if (has) {
        // Display 1-based frame index so it matches the badge overlay.
        int f = app.current_frame + 1;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f * ui_scale());
        char frame_label[32];
        snprintf(frame_label, sizeof(frame_label), "%s", tr(Str::FrameSliderFmt));
        if (ImGui::SliderInt("##frame", &f, 1, n, frame_label)) {
            app.current_frame = f - 1;
            app.play_accum = 0.0;
        }
    }

    if (!has) ImGui::EndDisabled();
}

// ---- filmstrip: one thumbnail per frame -------------------------------------
static void draw_filmstrip(App& app, Renderer& renderer) {
    const int n = app.frame_count();
    if (n == 0) {
        ImGui::TextDisabled("%s", tr(Str::EmptyNoFrames));
        return;
    }
    // No visible scrollbar: the mouse wheel scrolls the strip horizontally
    // (handled below), so a scrollbar never appears even with many frames.
    const float sc = ui_scale();
    ImGui::BeginChild("strip_scroll", ImVec2(0, 0), false,
                      ImGuiWindowFlags_NoScrollbar);
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive()) {
        float w = ImGui::GetIO().MouseWheel;
        if (w != 0.0f) {
            ImGui::SetScrollX(ImGui::GetScrollX() + w * 56.0f * sc);
        }
    }
    const float thumb_h = 64.0f * sc;
    for (int i = 0; i < n; i++) {
        ImGui::BeginGroup();
        const RgbaImage& img = app.doc.frame_images[i].image;
        if (img.empty()) { ImGui::EndGroup(); continue; }
        void* tex = renderer.texture_for(img, i);
        float w = img.height > 0 ? img.width * (thumb_h / (float)img.height)
                                 : 64.0f * sc;
        if (w < 1.0f) w = 1.0f;

        const bool cur = (app.current_frame == i);
        if (cur) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.55f, 0.95f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.85f, 0.25f, 1.0f));
        }
        char id[32];
        snprintf(id, sizeof(id), "##frame_%d", i);
        if (ImGui::ImageButton(id, tex, ImVec2(w, thumb_h),
                               ImVec2(0, 0), ImVec2(1, 1),
                               ImVec4(0, 0, 0, 0))) {
            app.current_frame = i;
            app.play_accum = 0.0;
        }
        // Tooltip is bound to the thumbnail button (the last widget drawn
        // before this check).
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(tr(Str::FilmstripTooltip), i + 1,
                              app.effective_frame_duration_ms(i),
                              app.doc.frame_images[i].duration_ms > 0
                                  ? tr(Str::TooltipExplicit)
                                  : tr(Str::TooltipDefault));
        if (cur) {
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
        }
        // Per-frame duration badge: absolute positioned in the bottom-right
        // corner of the thumbnail so it never affects layout or scrollbars.
        int dms = app.effective_frame_duration_ms(i);
        char cap[24];
        snprintf(cap, sizeof(cap), "%dms", dms);
        ImVec2 btn_min = ImGui::GetItemRectMin();   // screen coords
        ImVec2 cap_sz  = ImGui::CalcTextSize(cap);
        const float pad_x = 4.0f * sc, pad_y = 2.0f * sc;
        const float cx = btn_min.x + w - cap_sz.x - pad_x;
        const float cy = btn_min.y + thumb_h - ImGui::GetTextLineHeight() - pad_y - 1.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(
            ImVec2(cx - 2.0f, cy - 1.0f),
            ImVec2(cx + cap_sz.x + 2.0f, cy + ImGui::GetTextLineHeight() + 1.0f),
            IM_COL32(0, 0, 0, 150), 3.0f);
        dl->AddText(ImVec2(cx, cy), IM_COL32(255, 255, 255, 235), cap);
        ImGui::EndGroup();
        if (i < n - 1) ImGui::SameLine();
    }
    ImGui::EndChild();
}

// ---- right: metadata (all available attributes; empty ones skipped) ---------
static void draw_metadata_panel(App& app) {
    if (!app.has_document()) {
        ImGui::TextDisabled("%s", tr(Str::EmptyNoFile1));
        return;
    }
    const auto& d = app.doc;

    // Labeled key/value row; only rendered when the value is non-empty.
    auto kv = [](const char* label, const std::string& v) {
        if (!v.empty()) ImGui::TextWrapped("%s: %s", label, v.c_str());
    };

    kv(tr(Str::MetaTitle),       d.meta.title);
    kv(tr(Str::MetaGenerator),   d.meta.generator);
    kv(tr(Str::MetaVersion),     d.meta.version);
    kv(tr(Str::MetaContentType), d.meta.content_type);
    kv(tr(Str::MetaCreatedAt),   d.meta.created_at);
    kv(tr(Str::MetaUpdatedAt),   d.meta.updated_at);
    kv(tr(Str::MetaPalette),     d.meta.palette_id);
    kv(tr(Str::MetaPaletteVersion), d.meta.palette_version);

    // 维度: normalize "2" / "2.5" / "3" -> "2D" / "2.5D" / "3D".
    if (!d.scene.dimension.empty()) {
        std::string dim = d.scene.dimension;
        std::string low = dim;
        for (auto& c : low) c = (char)::tolower((unsigned char)c);
        if (low.find('d') == std::string::npos) dim += "D";
        ImGui::TextWrapped("%s: %s", tr(Str::MetaDimension), dim.c_str());
    }
    if (d.scene.width > 0 && d.scene.height > 0)
        ImGui::TextWrapped(tr(Str::MetaCanvasFmt), d.scene.width, d.scene.height);
    kv(tr(Str::MetaColorDepth), d.scene.color_depth);
    kv(tr(Str::MetaCoordinate), d.scene.coordinate);
    if (d.version_major > 0)
        ImGui::TextWrapped(tr(Str::MetaContainerFmt), d.version_major, d.version_minor);
    if (d.thumbnail_preview_index >= 0)
        ImGui::TextWrapped(tr(Str::MetaThumbIndexFmt), d.thumbnail_preview_index);
    ImGui::TextWrapped(tr(Str::MetaFramesLayersFmt), app.frame_count(),
                       (int)d.layers.size());
}

static void draw_layer_panel(App& app) {
    if (app.doc.layers.empty()) {
        ImGui::TextDisabled("%s", tr(Str::LayersNone));
        return;
    }
    // Files with per-layer previews (texture_table flavor) can really exclude
    // hidden layers from the picture; classic files embed only composited
    // frames, so there the checkbox records state only.
    const bool live = app.doc.has_layer_previews();
    for (auto& ly : app.doc.layers) {
        bool v = ly.visible;
        if (ly.preview.empty()) {
            // No isolated pixels for this layer — toggling cannot change the
            // picture, so the checkbox is disabled with an explanation.
            ImGui::BeginDisabled();
            ImGui::Checkbox(ly.name.c_str(), &v);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tr(Str::LayerNoPreview));
        } else if (ImGui::Checkbox(ly.name.c_str(), &v) && v != ly.visible) {
            ly.visible = v;   // re-blend the composite immediately
            app.mark_layers_changed();
        }
        if (!ly.visible) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", tr(Str::LayerHidden));
        }
    }
    ImGui::TextWrapped("%s", tr(live ? Str::LayersNoteLive : Str::LayersNote));
}

// ---- top-level layout -------------------------------------------------------
void render_ui(App& app, Renderer& renderer) {
    // One-shot on the very first frame: apply the persisted config (view
    // toggles, panel widths, minimap offset, theme). Ran BEFORE anything
    // draws so a light theme from config.ini is active on the first
    // presented frame, not a dark flash.
    if (!app.view_config_applied) {
        app.view_config_applied = true;
        const ViewConfig c = load_view_config();
        app.show_list_panel = c.show_list;
        app.show_info_panel = c.show_info;
        app.show_minimap    = c.show_minimap;
        if (c.list_w > 0.0f) app.list_panel_width = c.list_w * ui_scale();
        if (c.info_w > 0.0f) app.info_panel_width = c.info_w * ui_scale();
        if (c.map_ox > 0.0f) app.minimap_off_x = c.map_ox * ui_scale();
        if (c.map_oy > 0.0f) app.minimap_off_y = c.map_oy * ui_scale();
        app.theme_mode = theme_mode_from_int(c.theme);
    }
    // Keep the renderer's theme in sync with the choice (immediate switch
    // from the menu; live-follows the OS while theme_mode == System).
    renderer.apply_theme(app.theme_mode);

    // Root window: fills the entire main viewport. Using an explicit root window
    // prevents ImGui from falling back to the implicit "Debug##Default" window,
    // whose position/size is saved in imgui.ini and can shrink the UI to a small
    // panel in the corner (as seen in the bug report screenshot).
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("main_root", nullptr,
                 ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_MenuBar);

    // Menu bar lives inside the root window so content is automatically placed
    // below it and the bar never overlaps the layout.
    if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu(tr(Str::MenuFile))) {
        if (ImGui::MenuItem(tr(Str::MenuOpen))) {
            std::string p = open_file_dialog();
            if (!p.empty()) app.load_file(p);
        }
        ImGui::Separator();
#ifdef _WIN32
        // Live association state — re-queried every time the menu opens.
        RegState rs = query_pxb_registration();
        // Tooltips must be attached while the status text is still ImGui's
        // "last item": the register/unregister MenuItems below would otherwise
        // steal IsItemHovered.
        auto assoc_status_tooltips = [&rs]() {
            if (!ImGui::IsItemHovered()) return;
            std::string tip;
            if (!rs.exe_path.empty()) tip += rs.exe_path;
            if (rs.userchoice_override) {
                if (!tip.empty()) tip += "\n";
                tip += tr(Str::AssocUserChoiceHint);
            }
            if (!tip.empty()) ImGui::SetTooltip("%s", tip.c_str());
        };
        if (rs.registered()) {
            ImGui::TextDisabled("%s", tr(Str::AssocRegistered));
            assoc_status_tooltips();
            if (ImGui::MenuItem(tr(Str::AssocUnregister))) run_association(false);
        } else {
            ImGui::TextDisabled("%s", tr(Str::AssocNotRegistered));
            assoc_status_tooltips();
            if (ImGui::MenuItem(tr(Str::AssocRegister))) run_association(true);
        }
        // A machine-wide open verb still points at us: double-click may still
        // launch the app even though the current user is clean.
        if (rs.hklm_open_verb) {
            ImGui::TextDisabled("%s", tr(Str::AssocSystemLevelLeftover));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tr(Str::AssocHklmLeftoverDetail));
        }
#else
        ImGui::TextDisabled("%s", tr(Str::AssocWinOnly));
#endif
        ImGui::Separator();
        if (ImGui::MenuItem(tr(Str::MenuQuit))) {
            SDL_Event q;
            q.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&q);
        }
        ImGui::EndMenu();
    }
        if (ImGui::BeginMenu(tr(Str::MenuView))) {
            // Toggle switches for the layout panels + the preview minimap.
            ImGui::MenuItem(tr(Str::MenuMinimap), nullptr, &app.show_minimap);
            ImGui::MenuItem(tr(Str::MenuFileList), nullptr, &app.show_list_panel);
            ImGui::MenuItem(tr(Str::MenuInfoLayers), nullptr, &app.show_info_panel);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr(Str::MenuTheme))) {
            // Radio-style choice; takes effect next frame via apply_theme.
            // "Follow system" resolves against the OS on Windows and falls
            // back to dark on other platforms (see theme.cpp).
            if (ImGui::MenuItem(tr(Str::ThemeSystem), nullptr,
                                app.theme_mode == ThemeMode::System))
                app.theme_mode = ThemeMode::System;
            if (ImGui::MenuItem(tr(Str::ThemeDark), nullptr,
                                app.theme_mode == ThemeMode::Dark))
                app.theme_mode = ThemeMode::Dark;
            if (ImGui::MenuItem(tr(Str::ThemeLight), nullptr,
                                app.theme_mode == ThemeMode::Light))
                app.theme_mode = ThemeMode::Light;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr(Str::MenuLang))) {
            const bool zh = (current_lang() == Lang::zh);
            if (ImGui::MenuItem(tr(Str::LangZh), nullptr, zh)) set_lang(Lang::zh);
            if (ImGui::MenuItem(tr(Str::LangEn), nullptr, !zh)) set_lang(Lang::en);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr(Str::MenuHelp))) {
            if (ImGui::MenuItem(tr(Str::MenuAbout))) {
#ifdef _WIN32
                show_about_dialog(renderer.window_handle());   // real OS window
#else
                app.show_about = true;   // non-Windows fallback: ImGui window
#endif
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // ---- keyboard shortcuts ---------------------------------------------------
    // ←/→ = previous/next sibling .pxb (wraps, follows the search filter);
    // Esc = close the current window (About first, else the app window).
    // Skipped while a text field owns the keyboard (search box) or a
    // menu/popup is open (ImGui closes those on Esc / uses arrows for nav).
    const ImGuiIO& kio = ImGui::GetIO();
    if (!kio.WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            if (app.show_about) {
                app.show_about = false;
            } else {
                SDL_Event q;
                q.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&q);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))  navigate_siblings(app, -1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) navigate_siblings(app, +1);
    }

    // --- layout sizing --------------------------------------------------------
    // Geometry constants live at file scope (kListWidth / kInfoWidth /
    // kFramesHeight / kPanelGap / kStatusHeight) so they are tuned in one place.

    // Conditional visibility (per requirements):
    //  - frame strip only when the document is multi-frame;
    //  - layer panel hidden when (single frame AND single layer), and also when
    //    the file carries no layer metadata at all.
    const bool multi        = app.frame_count() > 1;
    const bool single_fs    = (app.frame_count() <= 1) && (app.doc.layers.size() <= 1);
    const bool show_frames  = multi;
    const bool show_layers  = !app.doc.layers.empty() && !single_fs;

    // All hand-set geometry is authored at 100% DPI (constants above); scale
    // it here so panel widths/heights keep their designed proportions on a
    // 150%/175% display. The scaled values are what the layout below uses.
    const float s          = ui_scale();
    const float frames_h   = kFramesHeight * s;
    const float panel_gap  = kPanelGap * s;
    const float status_h   = kStatusHeight * s;

    // --- side panels: View-menu switches + draggable widths --------------------
    // Widths live in App (user-adjusted via the splitters below, preview
    // pixels). <=0 = first use → the kListWidth/kInfoWidth defaults. Every
    // frame both are clamped so the preview and the OTHER panel always keep
    // their minimum width.
    const float splitter_w    = 6.0f * s;
    const float min_panel_w   = 100.0f * s;
    const float min_preview_w = 80.0f * s;
    const int   n_split =
        (app.show_list_panel ? 1 : 0) + (app.show_info_panel ? 1 : 0);
    if (app.list_panel_width <= 0.0f) app.list_panel_width = kListWidth * s;
    if (app.info_panel_width <= 0.0f) app.info_panel_width = kInfoWidth * s;
    const float main_w = ImGui::GetContentRegionAvail().x;
    auto clamp_panel_w = [&](float w, bool other_visible) {
        const float mx = main_w - min_preview_w - splitter_w * (float)n_split
                       - (other_visible ? min_panel_w : 0.0f);
        return std::clamp(w, min_panel_w, std::max(min_panel_w, mx));
    };

    const float list_w = app.show_list_panel
        ? clamp_panel_w(app.list_panel_width, app.show_info_panel) : 0.0f;
    const float info_w = app.show_info_panel
        ? clamp_panel_w(app.info_panel_width, app.show_list_panel) : 0.0f;
    if (app.show_list_panel) app.list_panel_width = list_w;
    if (app.show_info_panel) app.info_panel_width = info_w;

    float preview_w = main_w
                    - (app.show_list_panel ? list_w + splitter_w : 0.0f)
                    - (app.show_info_panel ? info_w + splitter_w : 0.0f);
    if (preview_w < min_preview_w) preview_w = min_preview_w;

    // Reserve space below the main row for the (optional) frames panel and the
    // always-visible status bar, so the main row fills the remaining height.
    const float reserve_below =
        (show_frames ? (frames_h + panel_gap) : 0.0f) + panel_gap + status_h;

    // Main row: [ pxb文件 | pxb预览 | pxb信息(+图层) ]
    // Side panels are optional (View menu); drag the splitters between them
    // to resize. The splitters replace the old fixed panel_gap.
    ImGui::BeginChild("main", ImVec2(0, -reserve_below),
                      false, ImGuiWindowFlags_NoScrollbar);
    const float row_h = ImGui::GetContentRegionAvail().y;

    // Left: sibling .pxb list with thumbnails (wheel scrolls, no scrollbar).
    if (app.show_list_panel) {
    ImGui::BeginChild("list", ImVec2(list_w, row_h), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // Search box replaces the static "文件列表" title. Empty = show all
    // siblings (the original behavior); typing filters by filename.
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##list_search", tr(Str::SearchPlaceholder),
                             app.sibling_filter, sizeof(app.sibling_filter));
    ImGui::Separator();
    // Wheel scrolls the list vertically; we disable ImGui's default wheel
    // scroll (NoScrollWithMouse) so only this handler drives it.
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive()) {
        float w = ImGui::GetIO().MouseWheel;
        if (w != 0.0f) {
            ImGui::SetScrollY(ImGui::GetScrollY() - w * 48.0f * s);
        }
    }
    {
        const auto& sibs = app.sibling_pxb();
        // Filtered index list (shared with the ←/→ keyboard navigation).
        const std::vector<size_t>& filtered = filtered_siblings(app);

        if (filtered.empty()) {
            ImGui::TextDisabled("%s", tr(Str::ListEmpty));
        } else {
            const float thumb = 44.0f * s;
            // Placeholder rect fill must read against the panel in either
            // theme (dark value is the original look).
            const ImVec4 ph_col = is_dark_theme(app.theme_mode)
                                      ? ImVec4(0.22f, 0.22f, 0.25f, 1.0f)
                                      : ImVec4(0.87f, 0.87f, 0.90f, 1.0f);
            // --- row geometry (fixed height for the clipper) -----------------
            // Each row: a square thumbnail box on the left (art fitted inside
            // at max-w/max-h, centered — never stretched), then the title
            // block — always two text lines tall (name clamped with "…") —
            // and below it one smaller metadata line (size · mtime).
            const float line_h   = ImGui::GetTextLineHeight();
            const float font_sz  = ImGui::GetFontSize();
            const float meta_fs  = font_sz * 0.78f;   // smaller meta font
            const float meta_h   = meta_fs / font_sz * line_h;
            const float row_gap  = 3.0f * s;
            const float row_h    = 2.0f * line_h + row_gap + meta_h;
            const float thumb_gap = 6.0f * s;         // thumbnail <-> text gap
            const float thumb_oy = std::max(0.0f, (row_h - thumb) * 0.5f);
            // Only lay out the rows actually visible in the viewport, so a
            // directory with thousands of .pxb files costs the same per frame as a
            // handful (this is the P1 fix from the memory review: no full-list
            // traversal / per-item work every frame).
            ImGuiListClipper clipper;
            clipper.Begin((int)filtered.size(), row_h);
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    size_t i = filtered[row];
                    const std::string& p = sibs[i];
                    const std::string name = path_filename(p);
                    const bool cur = (p == app.current_path);

                    // Thumbnail: the currently-open document uses its own decoded
                    // thumbnail; everything else comes from the (lazy, hash-keyed)
                    // MetaCache. A cache miss triggers a PARTIAL decompress that
                    // decodes only the embedded thumbnail — never the animation
                    // frames. Once uploaded to the GPU we drop the CPU pixels to
                    // keep RAM bounded.
                    int tex_key = -1;
                    const RgbaImage* ti = nullptr;
                    std::shared_ptr<CachedMeta> cm;
                    if (cur && !app.doc.thumbnail.empty()) {
                        ti = &app.doc.thumbnail;
                        tex_key = App::kDocThumbTexKey;  // matches the preview fallback key
                    } else {
                        cm = app.sibling_meta(p);
                        if (cm) {
                            ti = &cm->thumb;
                            tex_key = cm->gl_tex_key;
                        }
                    }
                    void* tex = (ti && !ti->empty())
                                    ? renderer.texture_for(*ti, tex_key)
                                    : nullptr;
                    if (tex && cm && !cm->thumb.pixels.empty())
                        cm->thumb.pixels.clear();   // GPU now owns the pixels

                    const ImVec2 row_pos = ImGui::GetCursorPos();
                    const float row_w = ImGui::GetContentRegionAvail().x;
                    const float text_x = thumb + thumb_gap;
                    const float text_w = row_w - text_x - 2.0f * s;

                    // --- thumbnail (left): SQUARE box, art fitted + centered ---
                    // The fitted rect keeps the source aspect (max 100% of the
                    // box in both axes); the InvisibleButton over it owns the
                    // click, and the non-square sources (doc thumbnail) are
                    // never stretched.
                    ImGui::SetCursorPos(ImVec2(row_pos.x, row_pos.y + thumb_oy));
                    if (tex) {
                        const float k = std::min(thumb / (float)ti->width,
                                                 thumb / (float)ti->height);
                        const ImVec2 isz((float)ti->width * k, (float)ti->height * k);
                        ImGui::SetCursorPos(ImVec2(row_pos.x + (thumb - isz.x) * 0.5f,
                                                   row_pos.y + thumb_oy + (thumb - isz.y) * 0.5f));
                        ImGui::Image(tex, isz);
                        ImGui::SetCursorPos(ImVec2(row_pos.x, row_pos.y + thumb_oy));
                        if (ImGui::InvisibleButton(("##thumb_" + std::to_string(i)).c_str(),
                                                   ImVec2(thumb, thumb))) {
                            if (p != app.current_path) app.load_file(p);
                        }
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ph_col);
                        if (ImGui::Button(("##ph_" + std::to_string(i)).c_str(),
                                          ImVec2(thumb, thumb))) {
                            if (p != app.current_path) app.load_file(p);
                        }
                        ImGui::PopStyleColor();
                    }

                    // --- title + metadata block (right) ------------------------
                    // One InvisibleButton owns hover/click for the whole block;
                    // the current-file/hover highlight is drawn behind the text.
                    ImGui::SetCursorPos(ImVec2(row_pos.x + text_x, row_pos.y));
                    ImGui::InvisibleButton(("##row_" + std::to_string(i)).c_str(),
                                           ImVec2(text_w, row_h));
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        ImGui::SetTooltip("%s", p.c_str());
                    }
                    if (ImGui::IsItemClicked(0) && p != app.current_path)
                        app.load_file(p);
                    if (cur || ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                        const ImVec2 r0 = ImGui::GetItemRectMin();
                        const ImVec2 r1 = ImGui::GetItemRectMax();
                        const ImU32 col = ImGui::GetColorU32(
                            cur ? ImGuiCol_Header
                                : ImGui::IsItemActive() ? ImGuiCol_HeaderActive
                                                        : ImGuiCol_HeaderHovered);
                        ImGui::GetWindowDrawList()->AddRectFilled(r0, r1, col);
                    }

                    // Filename: at most two wrapped lines, "…" when clamped.
                    // The block reserves two lines even for short names so the
                    // metadata line below stays at a fixed height.
                    const std::string disp_name =
                        clamp_wrapped(name, text_w, line_h, 2);
                    ImGui::SetCursorPos(ImVec2(row_pos.x + text_x, row_pos.y + 1.0f * s));
                    ImGui::PushTextWrapPos(row_pos.x + text_x + text_w);
                    ImGui::TextWrapped("%s", disp_name.c_str());
                    ImGui::PopTextWrapPos();

                    // Metadata line: "size | YYYY-MM-DD HH:mm" in a smaller font.
                    uint64_t fsize = 0;
                    int64_t fmtime_ns = 0;
                    std::string meta;
                    if (file_stat(p, &fmtime_ns, &fsize))
                        meta = format_bytes(fsize) + " | " +
                               format_mtime(fmtime_ns / 1000000000);
                    if (!meta.empty()) {
                        ImGui::SetCursorPos(ImVec2(row_pos.x + text_x,
                                                   row_pos.y + 1.0f * s +
                                                       2.0f * line_h + row_gap));
                        ImGui::GetWindowDrawList()->AddText(
                            ImGui::GetFont(), meta_fs, ImGui::GetCursorScreenPos(),
                            ImGui::GetColorU32(ImGuiCol_TextDisabled), meta.c_str());
                    }

                    // Fixed row height for the clipper's position math.
                    ImGui::SetCursorPos(ImVec2(row_pos.x, row_pos.y + row_h));
                }
            }
            clipper.End();
        }
    }
    ImGui::EndChild();  // list

    // Drag to resize the left list (only when it is followed by more panels —
    // the preview is always present, so it always is).
    const float dl_list = panel_splitter("##split_list", row_h);
    if (dl_list != 0.0f)
        app.list_panel_width = clamp_panel_w(app.list_panel_width + dl_list,
                                             app.show_info_panel);
    } // show_list_panel

    // Center: preview (no scrollbar; wheel = zoom).
    // Heights are passed explicitly (row_h, not 0/auto): a 0-height child
    // after a SameLine following a full-height InvisibleButton resolves to
    // zero remaining space in ImGui 1.91, which blanked the whole row.
    ImGui::BeginChild("preview", ImVec2(preview_w, row_h), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // No panel title / separator: the preview area is the canvas — everything
    // from the top edge belongs to the image.
    draw_image_view(app, renderer);
    ImGui::EndChild();  // preview

    // Right: pxb信息 (full height, may stack 图层 below when shown).
    if (app.show_info_panel) {
    const float dl_info = panel_splitter("##split_info", row_h);
    if (dl_info != 0.0f)
        app.info_panel_width = clamp_panel_w(app.info_panel_width - dl_info,
                                             app.show_list_panel);
    ImGui::BeginChild("info_col", ImVec2(info_w, row_h), false);
    {
        const float col_h = ImGui::GetContentRegionAvail().y;
        // Layer panel needs enough room for title + checkbox(es) + note;
        // keep a generous height so the info panel still shows all metadata.
        const float layer_h = show_layers ? 126.0f * s : 0.0f;
        float info_h = col_h - layer_h;
        if (info_h < 120.0f * s) info_h = 120.0f * s;

        if (show_layers) {
            ImGui::BeginChild("info", ImVec2(0, info_h), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextUnformatted(tr(Str::PanelInfo));
            ImGui::Separator();
            draw_metadata_panel(app);
            ImGui::EndChild();
            ImGui::BeginChild("layers", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextUnformatted(tr(Str::PanelLayers));
            ImGui::Separator();
            draw_layer_panel(app);
            ImGui::EndChild();
        } else {
            ImGui::BeginChild("info", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextUnformatted(tr(Str::PanelInfo));
            ImGui::Separator();
            draw_metadata_panel(app);
            ImGui::EndChild();
        }
    }
    ImGui::EndChild();  // info_col
    } // show_info_panel

    ImGui::EndChild();  // main

    // Bottom: 帧 panel — only when the document is multi-frame.
    if (show_frames) {
        ImGui::BeginChild("frames", ImVec2(0, frames_h), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::TextUnformatted(tr(Str::PanelFrames));
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(12.0f * s, 0.0f));
        ImGui::SameLine();
        draw_playback(app);     // transport: play/pause/stop/seek + frame slider
        ImGui::Separator();
        draw_filmstrip(app, renderer);
        ImGui::EndChild();
    }

    // Always-visible status bar: surfaces load/open results (success or error)
    // that previously were only written to the log file, so the user knows
    // why a file did or did not open.
    ImGui::BeginChild("status", ImVec2(0, status_h), false, ImGuiWindowFlags_NoScrollbar);
    {
        const float y = (status_h - ImGui::GetTextLineHeight()) * 0.5f;
        if (app.mem_working_mb > 0) {
            char mem[64];
            snprintf(mem, sizeof(mem), "WS %d MB | PV %d MB",
                     app.mem_working_mb, app.mem_private_mb);
            char cache[96];
            snprintf(cache, sizeof(cache), tr(Str::StatusBarCacheFmt),
                     app.meta_cache().size(), app.meta_cache().cap(),
                     app.meta_cache().mem_bytes() / (1024 * 1024));
            ImVec2 mem_sz = ImGui::CalcTextSize(mem);
            ImVec2 cache_sz = ImGui::CalcTextSize(cache);
            float gap = 12.0f;
            float avail_x = ImGui::GetContentRegionAvail().x;
            float right_x = std::max(0.0f, avail_x - mem_sz.x - gap - cache_sz.x - 8.0f);
            float mem_x = right_x + cache_sz.x + gap;

            // Cache stats + memory counters on the right.
            ImGui::SetCursorPos(ImVec2(right_x, y));
            ImGui::TextDisabled("%s", cache);
            ImGui::SetCursorPos(ImVec2(mem_x, y));
            ImGui::TextDisabled("%s", mem);

            // Status message on the left, wrapped so it never overlaps.
            ImGui::SetCursorPos(ImVec2(0, y));
            ImGui::PushTextWrapPos(right_x - 4.0f);
            if (!app.status_msg.empty())
                ImGui::TextWrapped("%s", app.status_msg.c_str());
            else
                ImGui::TextDisabled("%s", tr(Str::StatusReady));
            ImGui::PopTextWrapPos();
        } else {
            ImGui::SetCursorPosY(y);
            if (!app.status_msg.empty())
                ImGui::TextWrapped("%s", app.status_msg.c_str());
            else
                ImGui::TextDisabled("%s", tr(Str::StatusReady));
        }
    }
    ImGui::EndChild();

    ImGui::End();  // main_root

    // ---- About window --------------------------------------------------------
    // A plain, non-modal window (NOT a popup): popups opened while the menu bar
    // is closing are dropped by ImGui, which made the old "About" appear dead.
    // Drawn AFTER main_root ends so it floats above the full-screen root.
    if (app.show_about) {
        ImGui::SetNextWindowSize(ImVec2(380.0f * ui_scale(), 0), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("about_window", &app.show_about,
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoResize |      // fixed size
                         ImGuiWindowFlags_NoCollapse)) {  // no minimize/collapse
            // Pixel World icon, downscaled to 128x128 once at startup.
            const RgbaImage& ic = about_icon_image();
            if (!ic.empty()) {
                void* tex = renderer.texture_for(ic, 0xC001);
                ImGui::Image(tex, ImVec2(64.0f * ui_scale(), 64.0f * ui_scale()));
                ImGui::SameLine();
            }
            ImGui::BeginGroup();
            ImGui::TextUnformatted(kAppName);
            ImGui::Separator();
            char line[160];
            snprintf(line, sizeof(line), tr(Str::AboutVersionFmt), kAppVersion);
            ImGui::TextUnformatted(line);
            ImGui::TextUnformatted(tr(Str::AboutPowered));
            snprintf(line, sizeof(line), tr(Str::AboutBuildFmt), __DATE__);
            ImGui::TextDisabled("%s", line);
            ImGui::EndGroup();
            ImGui::Spacing();
            ImGui::TextWrapped("%s: %s", tr(Str::AboutHomepage), kHomepage);
            if (ImGui::Button(tr(Str::AboutOpenHomepage)))
                SDL_OpenURL(kHomepage);
            ImGui::Spacing();
            ImGui::TextWrapped("%s: %s", tr(Str::AboutCopyright), kAppCopyright);
            ImGui::TextWrapped("%s: %s", tr(Str::AboutAuthor), kAppAuthor);
            ImGui::TextWrapped("%s: %s", tr(Str::AboutLicense), tr(Str::LicenseValue));
            ImGui::Spacing();
            if (ImGui::Button(tr(Str::AboutClose)))
                app.show_about = false;
        }
        ImGui::End();
    }
}

// ---- persisted view config ---------------------------------------------------
// Convert the live App layout state back into DPI-independent logical units
// and write it out. Called once at normal application exit (main.cpp). The
// view_config_applied gate prevents clobbering the file before the UI has
// ever run (e.g. the headless --register path never reaches here).
void save_view_settings(const App& app) {
    if (!app.view_config_applied) return;
    const float s = ui_scale();
    ViewConfig c;
    c.show_list    = app.show_list_panel;
    c.show_info    = app.show_info_panel;
    c.show_minimap = app.show_minimap;
    c.list_w = app.list_panel_width / s;
    c.info_w = app.info_panel_width / s;
    c.map_ox = app.minimap_off_x / s;
    c.map_oy = app.minimap_off_y / s;
    c.theme  = (int)app.theme_mode;
    save_view_config(c);
}

} // namespace pxb