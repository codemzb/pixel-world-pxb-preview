// renderer.cpp
#include "renderer.h"
#include "gl_loader.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

#include <SDL3/SDL.h>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif

namespace pxb {

Renderer::Renderer() = default;
Renderer::~Renderer() { shutdown(); }

bool Renderer::init(int width, int height, const std::string& title, std::string& err) {
    width_ = width; height_ = height;
    err.clear();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        err = std::string("SDL_Init(VIDEO) failed: ") + SDL_GetError();
        return false;
    }

    // OpenGL 2.1 + GLSL 1.20: maximum compatibility. Works on virtual machines,
    // RDP, cloud desktops, headless servers, old GPUs, and Intel HD on Win7+.
    // Modern hardware also accepts this profile so nothing is lost.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    // ---- Initial window size ------------------------------------------------
    // On Windows (and X11) window coordinates are physical pixels: a "logical
    // 840x540" window must be created pre-multiplied by the display content
    // scale, or the app opens at its 100%-DPI size on a 175% screen — exactly
    // the "window never scales with DPI" symptom. On Apple platforms SDL sizes
    // windows in logical points and HIGH_PIXEL_DENSITY alone drives the backing
    // resolution, so the size must stay unscaled there.
    float create_scale = 1.0f;
#ifndef __APPLE__
    {
        SDL_DisplayID did = SDL_GetPrimaryDisplay();
        float s = (did != 0) ? SDL_GetDisplayContentScale(did) : 1.0f;
        if (s > 0.0f) create_scale = s;
    }
#endif
    const int create_w = (int)lroundf((float)width * create_scale);
    const int create_h = (int)lroundf((float)height * create_scale);

    window_ = SDL_CreateWindow(title.c_str(), create_w, create_h,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                               SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        err = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
        return false;
    }

    gl_ = SDL_GL_CreateContext(window_);
    if (!gl_) {
        err = std::string("SDL_GL_CreateContext failed (no OpenGL 2.1 driver?): ") + SDL_GetError();
        return false;
    }
    if (!SDL_GL_MakeCurrent(window_, gl_)) {
        err = std::string("SDL_GL_MakeCurrent failed: ") + SDL_GetError();
        return false;
    }
    SDL_GL_SetSwapInterval(1);

    if (!gl_loader_init()) {
        err = "gl_loader_init failed: missing required GL 1.1 entry points (opengl32.dll broken?)";
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imgui_ctx_ = ImGui::GetCurrentContext();
    ImGuiIO& io = ImGui::GetIO();
    // Note: ImGuiConfigFlags_DockingEnable only exists on the imgui "docking"
    // branch; we build against master, so docking is intentionally off.
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Disable imgui.ini entirely: every window we create uses
    // ImGuiWindowFlags_NoSavedSettings, so there is nothing worth persisting —
    // writing the file only litters the working directory with identical
    // imgui.ini copies wherever the exe happens to be launched from.
    io.IniFilename = nullptr;

    // ---- Detect display DPI scale and apply it to ImGui style ---------------
    // SDL_GetDisplayContentScale returns the display's content scaling factor
    // (1.0 = 100%, 1.5 = 150%, 2.0 = 200%). On non-DPI-aware systems it
    // returns 1.0. We query the display the window was created on; if that
    // fails we fall back to 1.0 so the app still works.
    {
        SDL_DisplayID did = SDL_GetDisplayForWindow(window_);
        float scale = (did != 0) ? SDL_GetDisplayContentScale(did) : 1.0f;
        if (scale <= 0.0f) scale = 1.0f;
        dpi_scale_ = scale;
#ifndef __APPLE__
        if (fabsf(scale - create_scale) > 0.001f) {
            // Launched on a monitor whose scale differs from the primary —
            // re-fit the window to the display it actually landed on.
            SDL_SetWindowSize(window_,
                              (int)lroundf((float)width * scale),
                              (int)lroundf((float)height * scale));
        }
#endif
    }

    // Scale ImGui style sizes (padding, rounding, borders, scrollbar, etc.)
    // BEFORE any fonts are loaded. ScaleAllSizes is multiplicative and must be
    // applied to a freshly-reset style, which it already is at this point.
    ImGui::GetStyle().ScaleAllSizes(dpi_scale_);

    // ---- Load fonts at DPI-aware size ---------------------------------------
    // The GL3 backend must be initialized BEFORE rebuild_fonts(): the font
    // upload inside it calls the backend's GL entry points, which are only
    // resolved by ImGui_ImplOpenGL3_Init. Calling rebuild_fonts() first used
    // to crash (null GL function pointer) when the atlas upload was made
    // eager at startup.
    ImGui_ImplSDL3_InitForOpenGL(window_, gl_);
    if (!ImGui_ImplOpenGL3_Init("#version 120")) {
        err = "ImGui_ImplOpenGL3_Init failed (GLSL 1.20 shader compile error)";
        return false;
    }
    gl3_backend_ready_ = true;

    rebuild_fonts();

    // Upload the font atlas to GPU now (instead of lazily at the first
    // NewFrame) so we can immediately discard the CPU-side pixel buffer.
    // The atlas can be ~5-10 MB even with the common-use subset; keeping it
    // on the CPU after upload is pure waste.
    ImGui_ImplOpenGL3_CreateFontsTexture();
    io.Fonts->ClearTexData();

    pfn_glClearColor(0.16f, 0.16f, 0.18f, 1.0f);
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(window_, &pw, &ph);
    pfn_glViewport(0, 0, pw, ph);
    return true;
}

void Renderer::rebuild_fonts() {
    // Load a CJK-capable font at a DPI-scaled size so Chinese/Japanese/Korean
    // filenames and titles render correctly on high-DPI displays.
    //
    // Base size is 16 logical pixels; multiplied by dpi_scale_ so the physical
    // pixel count matches the display's requested content scale (e.g. 24 px at
    // 150%, 32 px at 200%). Round to the nearest integer to avoid sub-pixel
    // blurring in the rasteriser.
    //
    // On a rebuild (display-change mid-session) the old atlas is cleared first
    // so we don't accumulate duplicate font entries.
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const float base_size = 16.0f;
    const float font_size = floorf(base_size * dpi_scale_ + 0.5f);

    // Glyph ranges: a *common-use* subset rather than the full CJK Unified
    // Ideographs (which is ~20k glyphs and balloons the font atlas to
    // 50-90 MB of RAM). Covers printable ASCII, daily-use Hanzi (GB2312
    // equivalent), CJK punctuation, fullwidth forms, and a few smart quotes.
    static const ImWchar ranges[] = {
        0x0020, 0x007F,          // ASCII
        0x2018, 0x2026,          // ' ' ' " … etc.
        0x3000, 0x303F,          // CJK symbols & punctuation
        0x4E00, 0x9FFF,          // CJK Unified Ideographs (core)
        0xFF00, 0xFFEF,          // Fullwidth / Halfwidth forms
        0,
    };

    // Platform CJK font candidates: first one that loads wins. These are the
    // stock system fonts on each OS — missing files simply make AddFont fail
    // and the loop continues.
#if defined(_WIN32)
    const char* candidates[] = {
        "C:/Windows/Fonts/msyh.ttc",       // Microsoft YaHei (Win7+)
        "C:/Windows/Fonts/msyh.ttf",
        "C:/Windows/Fonts/msyhbd.ttc",
        "C:/Windows/Fonts/simhei.ttf",     // SimHei
        "C:/Windows/Fonts/simsun.ttc",     // SimSun
        "C:/Windows/Fonts/Deng.ttf",       // DengXian (Win8+)
    };
#elif defined(__APPLE__)
    const char* candidates[] = {
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/System/Library/Fonts/Supplemental/Songti.ttc",
        "/System/Library/Fonts/STHeiti Light.ttc",
    };
#else
    const char* candidates[] = {
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",   // Debian/Ubuntu fonts-noto-cjk
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",        // Fedora/openSUSE
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",           // WenQuanYi
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",          // last resort (ASCII only)
    };
#endif

    ImFont* cjk = nullptr;
    for (const char* path : candidates) {
        cjk = io.Fonts->AddFontFromFileTTF(path, font_size, nullptr, ranges);
        if (cjk) break;
    }
    if (cjk) io.Fonts->Build();
    // If no CJK font is found the default (ASCII-only ProggyClean) is used;
    // the app still works, just non-ASCII chars show as '?' boxes.

    // Upload the rebuilt atlas to GPU and free the CPU-side pixel buffer.
    // On a mid-session DPI change the backend is live. On the very first
    // init() this may still run before ImGui_ImplOpenGL3_Init (when init
    // ordering changes in the future) — the gl3_backend_ready_ flag makes
    // that safe by deferring the upload; the atlas then uploads lazily at
    // the first NewFrame.
    if (gl3_backend_ready_ && imgui_ctx_ && ImGui::GetCurrentContext()) {
        ImGui_ImplOpenGL3_DestroyFontsTexture();
        ImGui_ImplOpenGL3_CreateFontsTexture();
        io.Fonts->ClearTexData();
    }
}

void Renderer::shutdown() {
    if (pfn_glDeleteTextures) {
        for (auto& kv : tex_cache_) {
            if (kv.second) {
                GLuint t = kv.second;
                pfn_glDeleteTextures(1, &t);
            }
        }
    }
    tex_cache_.clear();

    if (imgui_ctx_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        imgui_ctx_ = nullptr;
    }
    if (gl_) { SDL_GL_DestroyContext(gl_); gl_ = nullptr; }
    if (window_) { SDL_DestroyWindow(window_); window_ = nullptr; }
    SDL_Quit();
}

bool Renderer::begin_frame() {
    SDL_Event e;
    bool quit = false;
    while (SDL_PollEvent(&e)) {
        ImGui_ImplSDL3_ProcessEvent(&e);
        if (e.type == SDL_EVENT_QUIT) quit = true;
        if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) quit = true;
        if (e.type == SDL_EVENT_DROP_FILE && on_drop_file) {
            // SDL3: SDL_DropEvent.data is a UTF-8 file path string. Unlike
            // SDL2, the app must NOT SDL_free it — SDL3 owns the string as
            // "temporary memory" and frees it on the next event pump
            // (SDL_FreeTemporaryMemory in SDL_PumpEventsInternal). It stays
            // valid for the duration of this synchronous callback; adding an
            // SDL_free here would be a double free.
            if (e.drop.data) on_drop_file(e.drop.data);
        }
        // When the window moves to a monitor with a different DPI, rebuild
        // fonts and rescale ImGui style to match the new content scale.
        // SDL_EVENT_WINDOW_DISPLAY_CHANGED fires exactly once per transition.
        if (e.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) {
            SDL_DisplayID did = SDL_GetDisplayForWindow(window_);
            float new_scale = (did != 0) ? SDL_GetDisplayContentScale(did) : 1.0f;
            if (new_scale <= 0.0f) new_scale = 1.0f;
            if (new_scale != dpi_scale_) {
                // Undo the previous ScaleAllSizes by applying the inverse, then
                // apply the new scale. This keeps the style multiplicatively
                // correct without accumulating floating-point drift.
                float ratio = new_scale / dpi_scale_;
                ImGui::GetStyle().ScaleAllSizes(ratio);
                dpi_scale_ = new_scale;
                rebuild_fonts();
            }
        }
    }
    if (quit) return false;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    return true;
}

void Renderer::end_frame() {
    int w = 0, h = 0;
    // Use physical pixels for the viewport so it matches the high-DPI
    // framebuffer SDL allocates. io.DisplaySize stays in logical pixels, so the
    // UI keeps its real-world size; mismatching the two is exactly what shrank
    // the whole UI on scaled displays (e.g. 150%).
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    pfn_glViewport(0, 0, w, h);
    pfn_glClear(GL_COLOR_BUFFER_BIT);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window_);
}

void Renderer::clear_textures() {
    if (pfn_glDeleteTextures) {
        for (auto& kv : tex_cache_) {
            if (kv.second) {
                GLuint t = kv.second;
                pfn_glDeleteTextures(1, &t);
            }
        }
    }
    tex_cache_.clear();
}

void Renderer::drop_texture(int key) {
    if (!pfn_glDeleteTextures) return;
    auto it = tex_cache_.find(key);
    if (it == tex_cache_.end() || !it->second) return;
    GLuint t = it->second;
    pfn_glDeleteTextures(1, &t);
    tex_cache_.erase(it);
}

void Renderer::clear_document_textures() {
    if (!pfn_glDeleteTextures) { tex_cache_.clear(); return; }
    for (auto it = tex_cache_.begin(); it != tex_cache_.end();) {
        int k = it->first;
        // Keep sibling-thumbnail textures (keys < -999); drop document keys.
        if (k >= 0 || k == -999) {
            GLuint t = it->second;
            pfn_glDeleteTextures(1, &t);
            it = tex_cache_.erase(it);
        } else {
            ++it;
        }
    }
}

void* Renderer::texture_for(const RgbaImage& img, int key) {
    unsigned int tex = 0;
    auto it = tex_cache_.find(key);
    if (it != tex_cache_.end()) {
        // Cache hit: the texture object already holds the pixels. Image zoom
        // is handled by ImGui (it scales the displayed quad, not the texture),
        // so there is no need to re-upload. A document reload always goes
        // through clear_document_textures() first (see main.cpp), so a stale
        // texture is never reused with different pixels — re-uploading here
        // would just waste GPU bandwidth.
        tex = it->second;
        return (void*)(uintptr_t)tex;
    }

    // No CPU pixels available (e.g. already discarded) and no GPU texture
    // cached — there is nothing to display for this key.
    if (img.pixels.empty()) return nullptr;

    pfn_glGenTextures(1, &tex);
    tex_cache_[key] = tex;                 // tracked so shutdown frees it
    pfn_glBindTexture(GL_TEXTURE_2D, tex);
    pfn_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    pfn_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    pfn_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    pfn_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    pfn_glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    pfn_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, img.pixels.data());
    return (void*)(uintptr_t)tex;
}

void Renderer::upload_and_discard(RgbaImage& img, int key) {
    if (!img.empty()) texture_for(img, key);
    img.pixels.clear();
    // width/height are kept for layout.
}

void Renderer::upload_document(PxbDocument& doc) {
    for (size_t i = 0; i < doc.frame_images.size(); ++i) {
        upload_and_discard(doc.frame_images[i].image, (int)i);
    }
    if (!doc.thumbnail.empty()) {
        upload_and_discard(doc.thumbnail, -999);
    }
}

} // namespace pxb
