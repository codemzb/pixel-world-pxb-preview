// ui.cpp
//
// Compact previewer UI (three columns + optional bottom strip):
//   +----------+----------------------+-----------+
//   | pxb文件  | pxb预览               | pxb信息   |
//   | (scroll) |  (wheel = zoom,      | (narrow)  |
//   |  w/o bar |   drag = pan,        +-----------+
//   |          |   no scrollbar)      | 图层      |
//   |          |                      |  ☑ 图层1  |
//   +----------+----------------------+-----------+
//   | 帧  (only when multi-frame)                |
//   |  [transport] [帧率] + filmstrip            |
//   +--------------------------------------------+
//
// No container ever shows a scrollbar: the pxb list and filmstrip scroll via
// the mouse wheel; the preview zooms with the wheel and pans with left-drag
// (pan is clamped so the image never reveals empty space past its edges).
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

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
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
static constexpr float kListWidth    = 170.0f;  // left pxb-file list width
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

// ---- center: mouse-wheel-zoomable image view (no scrollbar) ------------------
static void draw_image_view(App& app, Renderer& renderer) {
    // Draw directly into the parent "preview" child (which supplies the border
    // and title). No scrollbar ever appears: the wheel zooms (below), and the
    // image is centered / clipped, so there is nothing to scroll.
    ImVec2 avail = ImGui::GetContentRegionAvail();

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

    // Mouse wheel = zoom (1.1x per notch, clamped to keep things sane).
    if (ImGui::IsWindowHovered()) {
        float w = ImGui::GetIO().MouseWheel;
        if (w != 0.0f) {
            float factor = (w > 0.0f) ? 1.1f : (1.0f / 1.1f);
            app.zoom *= factor;
            if (app.zoom < App::kZoomMin) app.zoom = App::kZoomMin;
            if (app.zoom > App::kZoomMax) app.zoom = App::kZoomMax;
        }
    }

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

    // Pick the source image for the current frame.
    const RgbaImage* img = nullptr;
    int key = -999;   // dedicated key for the thumbnail fallback
    if (!app.doc.frame_images.empty() && app.current_frame < app.frame_count()) {
        img = &app.doc.frame_images[app.current_frame].image;
        key = app.current_frame;
    } else if (!app.doc.thumbnail.empty()) {
        img = &app.doc.thumbnail;   // files with only a thumbnail (no frames)
    }

    if (!img || img->empty()) {
        ImGui::TextDisabled("%s", tr(Str::EmptyNoPreview));
        return;
    }

    void* tex = renderer.texture_for(*img, key);
    ImVec2 disp((float)img->width * app.zoom, (float)img->height * app.zoom);

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

    // Base position: centered when the image fits, panned (within clamp) when
    // it is larger than the preview area.
    ImVec2 pos;
    pos.x = (disp.x <= avail.x) ? (avail.x - disp.x) * 0.5f : app.pan_x;
    pos.y = (disp.y <= avail.y) ? (avail.y - disp.y) * 0.5f : app.pan_y;

    ImVec2 cur = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(cur.x + pos.x, cur.y + pos.y));
    ImGui::Image(tex, disp);
    ImGui::SetCursorPos(cur);

    // Zoom badge (top-left).
    char zoom_label[32];
    snprintf(zoom_label, sizeof(zoom_label), "%.0f%%", app.zoom * 100.0f);
    const float sc = ui_scale();
    ImGui::SetCursorPos(ImVec2(cur.x + 8.0f * sc, cur.y + 8.0f * sc));
    ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 0.9f), "%s", zoom_label);
    ImGui::SetCursorPos(cur);

    // Frame counter overlay (only when animating).
    if (app.frame_count() > 1) {
        char badge[32];
        snprintf(badge, sizeof(badge), "%d / %d",
                 app.current_frame + 1, app.frame_count());
        ImGui::SetCursorPos(ImVec2(cur.x + 8.0f * sc,
                                   cur.y + 8.0f * sc + ImGui::GetTextLineHeight() + 4.0f * sc));
        ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.85f, 0.9f), "%s", badge);
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

        // Fit
        if (ImGui::Button(labels[0])) {
            float sx = avail.x / (float)img->width;
            float sy = avail.y / (float)img->height;
            float z = std::min(sx, sy);
            if (z < App::kZoomMin) z = App::kZoomMin;
            if (z > App::kZoomMax) z = App::kZoomMax;
            app.zoom = z;
        }
        ImGui::SameLine();
        if (ImGui::Button(labels[1])) app.zoom = 1.0f;
        ImGui::SameLine();
        if (ImGui::Button(labels[2])) app.zoom = std::max(App::kZoomMin, app.zoom / 1.25f);
        ImGui::SameLine();
        if (ImGui::Button(labels[3])) app.zoom = std::min(App::kZoomMax, app.zoom * 1.25f);

        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(3);
        ImGui::SetCursorPos(cur);
    }
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
    for (auto& ly : app.doc.layers) {
        bool v = ly.visible;
        if (ImGui::Checkbox(ly.name.c_str(), &v) && v != ly.visible) {
            ly.visible = v;   // toggle takes effect immediately
        }
        if (!ly.visible) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", tr(Str::LayerHidden));
        }
    }
    ImGui::TextWrapped("%s", tr(Str::LayersNote));
}

// ---- top-level layout -------------------------------------------------------
void render_ui(App& app, Renderer& renderer) {
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
    const float list_w     = kListWidth * s;
    const float info_w     = kInfoWidth * s;
    const float frames_h   = kFramesHeight * s;
    const float panel_gap  = kPanelGap * s;
    const float status_h   = kStatusHeight * s;

    const float main_w = ImGui::GetContentRegionAvail().x;
    float preview_w = main_w - list_w - info_w - panel_gap * 2.0f;
    if (preview_w < 80.0f * s) preview_w = 80.0f * s;

    // Reserve space below the main row for the (optional) frames panel and the
    // always-visible status bar, so the main row fills the remaining height.
    const float reserve_below =
        (show_frames ? (frames_h + panel_gap) : 0.0f) + panel_gap + status_h;

    // Main row: [ pxb文件 | pxb预览 | pxb信息(+图层) ]
    ImGui::BeginChild("main", ImVec2(0, -reserve_below),
                      false, ImGuiWindowFlags_NoScrollbar);

    // Left: sibling .pxb list with thumbnails (wheel scrolls, no scrollbar).
    ImGui::BeginChild("list", ImVec2(list_w, 0), true,
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
        // Case-insensitive filter derived from the search box; empty = no filter.
        std::string filter;
        {
            const char* f = app.sibling_filter;
            for (int k = 0; f[k]; ++k) filter += (char)std::tolower((unsigned char)f[k]);
        }

        // Build the filtered index list once per frame (cheap string work; the
        // expensive per-file decompress/decoding is cached in MetaCache).
        static std::vector<size_t> filtered;
        filtered.clear();
        for (size_t i = 0; i < sibs.size(); ++i) {
            if (!filter.empty()) {
                std::string name = path_filename(sibs[i]);
                for (char& c : name) c = (char)std::tolower((unsigned char)c);
                if (name.find(filter) == std::string::npos) continue;
            }
            filtered.push_back(i);
        }

        if (filtered.empty()) {
            ImGui::TextDisabled("%s", tr(Str::ListEmpty));
        } else {
            const float thumb = 44.0f * s;
            // Only lay out the rows actually visible in the viewport, so a
            // directory with thousands of .pxb files costs the same per frame as a
            // handful (this is the P1 fix from the memory review: no full-list
            // traversal / per-item work every frame).
            ImGuiListClipper clipper;
            clipper.Begin((int)filtered.size());
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
                        tex_key = -999;            // matches the preview fallback key
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

                    // Thumbnail (left) — clickable.
                    ImGui::BeginGroup();
                    if (tex) {
                        if (ImGui::ImageButton(("##thumb_" + std::to_string(i)).c_str(),
                                               tex, ImVec2(thumb, thumb),
                                               ImVec2(0, 0), ImVec2(1, 1),
                                               ImVec4(0, 0, 0, 0))) {
                            if (p != app.current_path) app.load_file(p);
                        }
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.22f, 0.25f, 1.0f));
                        if (ImGui::Button(("##ph_" + std::to_string(i)).c_str(),
                                          ImVec2(thumb, thumb))) {
                            if (p != app.current_path) app.load_file(p);
                        }
                        ImGui::PopStyleColor();
                    }
                    ImGui::EndGroup();

                    // Filename (right) — clickable row, highlights the current file.
                    ImGui::SameLine(0, 6.0f * s);
                    if (ImGui::Selectable(name.c_str(), cur,
                                          ImGuiSelectableFlags_AllowDoubleClick,
                                          ImVec2(0, thumb))) {
                        if (p != app.current_path) app.load_file(p);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.c_str());
                }
            }
            clipper.End();
        }
    }
    ImGui::EndChild();  // list

    ImGui::SameLine(0, panel_gap);
    // Center: preview (no scrollbar; wheel = zoom).
    ImGui::BeginChild("preview", ImVec2(preview_w, 0), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextUnformatted(tr(Str::PanelPreview));
    ImGui::Separator();
    draw_image_view(app, renderer);
    ImGui::EndChild();  // preview

    ImGui::SameLine(0, panel_gap);
    // Right: pxb信息 (full height, may stack 图层 below when shown).
    ImGui::BeginChild("info_col", ImVec2(info_w, 0), false);
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

} // namespace pxb