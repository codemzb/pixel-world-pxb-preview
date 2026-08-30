// app.h
//
// Application state and high-level operations. UI and main loop drive this
// object; it owns the parsed document and playback state. This is a pure
// previewer: no file-manager UI, no in-app browser. Files are opened via
// OS file association (double-click), drag & drop onto the window, or the
// File > Open dialog.
//
#pragma once

#include "pxb_format.h"
#include "pxb_cache.h"   // MetaCache (lazy/partial-decompression metadata cache)

#include <functional>
#include <list>
#include <string>
#include <unordered_map>
#include <memory>

namespace pxb {

class Renderer;   // used only as a pointer; full definition in renderer.h

class App {
public:
    App();

    // Load a .pxb by filesystem path (UTF-8). Updates status_msg.
    // Multi-frame files start playing automatically.
    bool load_file(const std::string& path);

    // Called every frame with elapsed seconds. Advances animation when playing.
    void tick(double dt_seconds);

    // Stop playback: pause and rewind to frame 0.
    void stop_playback();

    bool has_document() const { return doc.ok(); }
    int frame_count() const { return doc.frame_count(); }

    // Per-frame display duration (ms). Uses the frame's explicit `duration_ms`
    // when the file provides it, otherwise falls back to the global fps
    // (1000/fps). This is what actually drives playback timing.
    int effective_frame_duration_ms(int frame_index) const {
        if (frame_index >= 0 && frame_index < frame_count() &&
            doc.frame_images[frame_index].duration_ms > 0)
            return doc.frame_images[frame_index].duration_ms;
        return (int)(1000.0 / (double)fps);
    }
    // Total animation length across all frames (ms), per effective durations.
    int total_duration_ms() const {
        int t = 0;
        for (int i = 0; i < frame_count(); i++) t += effective_frame_duration_ms(i);
        return t;
    }

    // Sibling .pxb files in the current file's directory, for the in-app list.
    // Refreshed on every load_file(); empty directory scans "." (cwd).
    const std::vector<std::string>& sibling_pxb() const { return siblings_; }
    std::string sibling_dir() const { return siblings_dir_; }
    void refresh_siblings();

    // Non-blocking read of a sibling .pxb's cached metadata + thumbnail. Returns
    // nullptr on a cache miss; a miss enqueues an async background prefetch
    // (CPU-only partial decompress — never a full frame decode) so the UI thread
    // never stalls, and the row renders a placeholder until the worker fills
    // the cache. The shared_ptr keeps the entry alive for the caller even if a
    // concurrent cache eviction erases it. Non-const access so the UI may
    // discard the CPU pixel buffer after uploading the texture to the GPU.
    std::shared_ptr<CachedMeta> sibling_meta(const std::string& path);

    // Accessor for the metadata cache (e.g. to surface cache size in the status
    // bar). Returns the live cache object.
    MetaCache& meta_cache() { return meta_cache_; }
    const MetaCache& meta_cache() const { return meta_cache_; }

    // Wire the owning Renderer (set once after construction). Needed so evicted
    // sibling GL textures can be deleted on the main thread.
    void set_renderer(Renderer* r) { renderer_ = r; }

    // Per-frame maintenance: drain GL texture keys dropped by cache eviction and
    // delete them on the main thread. Call once per frame (before rendering).
    void frame_cache_maintenance();

    // Optional hook called once after a document is successfully loaded and
    // before the first frame is rendered. Main.cpp wires this to the renderer
    // so the GPU textures can be built and the CPU pixel buffers discarded.
    std::function<void(PxbDocument&)> on_document_loaded;

    // ----------------------------------------------------------------------
    // NOTE ON NAMING CONVENTION
    // The public data members below (doc, current_path, current_frame, playing,
    // fps, zoom, pan_x, pan_y, status_msg, mem_working_mb, mem_private_mb,
    // sibling_filter, show_about, ...) intentionally OMIT the trailing
    // underscore that Google C++ Style prescribes for class members.
    //
    // This App is an "immediate-mode state object": the ImGui UI layer reads
    // and writes these fields directly every frame (e.g. app.zoom,
    // app.current_path), treating them more like the fields of a value type /
    // struct than encapsulated state. Adding trailing underscores would ripple
    // through every UI call site for no behavioral gain. The private members
    // (siblings_, siblings_dir_) DO follow the trailing-
    // underscore convention. If you later wrap these publics behind accessors,
    // switch them to the underscore style at the same time.
    // ----------------------------------------------------------------------

    // State exposed to the UI layer.
    PxbDocument doc;
    std::string current_path;
    int current_frame = 0;

    bool playing = false;
    float fps = 8.0f;
    double play_accum = 0.0;     // seconds accumulated within current frame

    // User-controlled image zoom (1.0 = native pixels). The main view renders
    // the current frame at width*zoom, height*zoom; the GL texture filter is
    // GL_NEAREST so the pixels stay crisp at any zoom.
    float zoom = 1.0f;
    static constexpr float kZoomMin = 0.05f;
    static constexpr float kZoomMax = 40.0f;

    // Set by reset_document_state(); consumed by draw_image_view() to fit the
    // first frame into the available preview area (reduces empty space).
    bool fit_next_frame = false;

    // Mouse-drag pan offset (in preview pixels) used when a zoomed-in image is
    // larger than the view. The view clamps pan so the image never reveals
    // empty space past its edges (see draw_image_view).
    float pan_x = 0.0f;
    float pan_y = 0.0f;

    std::string status_msg;

    // Live memory diagnostics (MB), updated by main.cpp each frame on Windows.
    int mem_working_mb = 0;   // Working Set
    int mem_private_mb = 0;   // Private Bytes

    // In-app file-list filter. Bound to the search box that replaces the
    // static "文件列表" title; empty = show all siblings (default behavior).
    char sibling_filter[256] = {0};

    // About window visibility. Set from the Help menu; the window itself is a
    // plain ImGui window (NOT a popup/modal — popups created while the menu
    // bar is closing are dropped by ImGui, which made "About" appear dead).
    bool show_about = false;

    ~App();

private:
    void reset_document_state();

    std::string siblings_dir_;           // last scanned directory
    std::vector<std::string> siblings_;  // full paths of .pxb files in that dir

    // Lazy/partial-decompression metadata cache (hash-keyed, LRU-capped). This
    // replaces the old per-path thumbnail LRU: list display reads metadata +
    // thumbnails from here, and full frame decompression is deferred to load_file.
    MetaCache meta_cache_;

    // Owning renderer, set via set_renderer(). Used to delete evicted sibling
    // GL textures on the main thread.
    Renderer* renderer_ = nullptr;

    // Background prefetch pool (CPU-only; never touches GL). Warms the metadata
    // cache for siblings so the list scrolls smoothly without synchronous stalls.
    class PrefetchPool* prefetch_ = nullptr;
};

} // namespace pxb
