// renderer.h
//
// Cross-platform rendering backend: SDL3 window + OpenGL3 + Dear ImGui.
// A small dependency stack (SDL3 + ImGui + stb + miniz) keeps the binary
// tiny and the build portable across Windows/Linux/macOS with one codebase -
// no per-OS graphics backends required.
//
#pragma once

#include "pxb_format.h"
#include "theme.h"
#include <functional>
#include <string>
#include <unordered_map>

struct SDL_Window;
struct SDL_GLContextState;   // SDL3: typedef struct SDL_GLContextState *SDL_GLContext;
struct ImGuiContext;

namespace pxb {

class Renderer {
public:
    Renderer();
    ~Renderer();

    // Create window + GL context + ImGui. Returns false on failure; on
    // failure, `err` is filled with a human-readable cause (also suitable
    // for a MessageBox). OpenGL target is 2.1 + GLSL 1.20 for max compat
    // (VM, RDP, cloud desktop, headless GPU).
    bool init(int width, int height, const std::string& title, std::string& err);
    void shutdown();

    // Begin a frame: pump SDL events. Returns false if the app should quit.
    bool begin_frame();
    void end_frame();           // render ImGui + swap

    // Called when a file is dragged & dropped onto the window (UTF-8 path).
    // Set by the application (main.cpp wires it to App::load_file).
    std::function<void(const std::string&)> on_drop_file;

    // Upload (cached by key) an RGBA image to a GL texture and return its
    // ImGui texture id. Same key reuses the existing texture object (no
    // re-upload). key<0 is a one-shot bucket (still tracked for cleanup).
    void* texture_for(const RgbaImage& img, int key = -1);

    // Upload an image and immediately discard its CPU-side pixels. Used to
    // free RAM after the GPU copy is in place. If the key is already cached,
    // the pixels are simply discarded.
    void upload_and_discard(RgbaImage& img, int key = -1);

    // Upload every frame + thumbnail of a document, then discard the CPU
    // pixel buffers. Call once right after load_file() succeeds.
    void upload_document(PxbDocument& doc);

    // Release all cached GL textures (e.g. when a new document is loaded).
    // Does NOT destroy the window/GL context.
    void clear_textures();

    // Delete a single cached GL texture by key (used when a cached sibling
    // thumbnail is evicted). Safe to call with an unknown/invalid key.
    void drop_texture(int key);

    // Delete only the DOCUMENT textures (keys >= 0 for frames, -999 for the
    // document thumbnail), leaving sibling-thumbnail textures (negative keys
    // below -999) intact. Called on document load so navigating files does not
    // thrash the sibling-list thumbnails.
    void clear_document_textures();

    void* window_handle() const { return (void*)window_; }
    int width() const { return width_; }
    int height() const { return height_; }
    float dpi_scale() const { return dpi_scale_; }

    // Apply the active UI theme: ImGui style colors + the GL clear color.
    // Idempotent — no-op unless the effective theme changed. When `mode` is
    // System the OS preference is re-checked (cached ~2 s), so an OS theme
    // switch applies while the app is running. Only touches colors, never the
    // DPI-scaled style metrics (ScaleAllSizes owns those).
    void apply_theme(ThemeMode mode);

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContextState* gl_ = nullptr;
    ImGuiContext* imgui_ctx_ = nullptr;
    // True once ImGui_ImplOpenGL3_Init() has resolved the backend's GL function
    // pointers. rebuild_fonts() must not touch the GL3 backend before that —
    // CreateFontsTexture calls the backend's (then still null) GL entries.
    bool gl3_backend_ready_ = false;
    int width_ = 0, height_ = 0;
    float dpi_scale_ = 1.0f;
    ThemeMode applied_theme_ = ThemeMode::Dark;   // theme currently in the style + clear color
    std::unordered_map<int, unsigned int> tex_cache_;   // key -> GL texture id

    // Rebuild the ImGui font atlas at the current dpi_scale_. Uploads to GPU
    // and discards the CPU buffer. Safe to call after the first init() and
    // whenever the display scale changes (e.g. window moved to another monitor).
    void rebuild_fonts();
};

} // namespace pxb
