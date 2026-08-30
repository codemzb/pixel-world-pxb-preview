// app.cpp
#include "app.h"
#include "pxb_reader.h"
#include "fileutil.h"
#include "i18n.h"
#include "renderer.h"   // Renderer (for GL texture deletion on eviction)

#include <algorithm>
#include <cstdio>
#include <thread>
#include <condition_variable>
#include <deque>
#include <set>
#include <vector>
#include <atomic>
#include <memory>

namespace pxb {

// ---------------------------------------------------------------------------
// Background prefetch pool (CPU-only). Drains a queue of sibling paths and warms
// the MetaCache via partial decompress. Workers NEVER touch the GL context — only
// the main thread deletes GL textures (via App::frame_cache_maintenance).
// ---------------------------------------------------------------------------
// Forward-declared in app.h as `class PrefetchPool` inside namespace pxb — so
// the definition MUST also live in namespace pxb (an anonymous-namespace struct
// would be a distinct, incomplete type and break `delete prefetch_`).
class PrefetchPool {
public:
    PrefetchPool(MetaCache* c, int n) : cache_(c) {
        for (int i = 0; i < n; ++i) workers_.emplace_back([this] { run(); });
    }
    ~PrefetchPool() { stop(); }

    void enqueue(const std::vector<std::string>& paths) {
        {
            std::lock_guard<std::mutex> lk(m_);
            for (const auto& p : paths)
                if (!seen_.count(p)) { seen_.insert(p); q_.push_back(p); }
        }
        cv_.notify_all();
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lk(m_);
            if (stop_) return;
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) if (t.joinable()) t.join();
    }

private:
    void run() {
        for (;;) {
            std::string p;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [this] { return stop_ || !q_.empty(); });
                if (stop_ && q_.empty()) return;
                if (q_.empty()) continue;
                p = std::move(q_.front());
                q_.pop_front();
            }
            bool ok = cache_->prefetch(p);   // CPU-only partial read; thread-safe
            {
                std::lock_guard<std::mutex> lk(m_);
                // Success: drop the dedup marker so the path can be re-prefetched
                // later (after the file is edited and its cache entry invalidated,
                // or after an LRU eviction). Failure: keep the marker so a
                // permanently broken file isn't re-read every frame by the UI's
                // miss-triggered enqueue.
                if (ok) seen_.erase(p);
            }
        }
    }

    MetaCache* cache_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::string> q_;
    std::set<std::string> seen_;
    std::vector<std::thread> workers_;
    bool stop_ = false;
};

App::App() : prefetch_(new PrefetchPool(&meta_cache_, 4)) {
    refresh_siblings();   // list .pxb files in the current working directory
}

App::~App() {
    delete prefetch_;
}

// Scan the directory of the currently loaded file (or "." when none is loaded)
// for sibling .pxb files. Results are cached per-directory so we don't re-walk
// the filesystem every frame. Newly discovered siblings are enqueued for
// background metadata prefetch.
void App::refresh_siblings() {
    std::string dir = current_path.empty() ? "." : path_parent(current_path);
    if (dir == siblings_dir_) return;   // already scanned this directory
    siblings_dir_ = dir;
    // Directory changed: drop the previous folder's cached entries (their GL
    // texture keys are queued for deletion on the main thread via
    // frame_cache_maintenance). The LRU cap bounds memory anyway, but clearing
    // here keeps RAM/GPU at one directory's worth instead of accumulating
    // across folders.
    meta_cache_.clear();
    siblings_.clear();
    for (auto& e : list_directory(dir)) {
        if (e.is_dir) continue;
        if (!is_pxb_file(e.name)) continue;
        siblings_.push_back(path_join(dir, e.name));
    }
    std::sort(siblings_.begin(), siblings_.end());
    if (prefetch_) prefetch_->enqueue(siblings_);
}

std::shared_ptr<CachedMeta> App::sibling_meta(const std::string& path) {
    // Non-blocking read only: a miss renders a placeholder this frame and
    // warms the cache via the prefetch pool, so the UI thread never performs
    // a synchronous partial decompress (which visibly stalls on large
    // directories). Full frame decompression is still confined to
    // load_file() -> read_pxb().
    auto c = meta_cache_.find(path);
    if (!c && prefetch_) prefetch_->enqueue(std::vector<std::string>{path});
    return c;
}

void App::frame_cache_maintenance() {
    // Drain GL texture keys that were dropped by cache eviction and delete them
    // on the main thread (GL is per-thread; workers must never call this).
    std::vector<int> drops;
    meta_cache_.collect_dropped_textures(drops);
    if (renderer_ && !drops.empty())
        for (int k : drops) renderer_->drop_texture(k);
}

void App::reset_document_state() {
    current_frame = 0;
    play_accum = 0.0;
    // Multi-frame animations start playing automatically; single-frame files
    // (or thumbnail-only files) are shown statically.
    playing = (frame_count() > 1);
    // Ask the UI to fit the image into the preview area once on load.
    zoom = 1.0f;
    fit_next_frame = true;
    // Reset pan so a freshly loaded document starts centered.
    pan_x = pan_y = 0.0f;
}

bool App::load_file(const std::string& path) {
    if (!is_pxb_file(path)) {
        status_msg = std::string(tr(Str::StatusNotPxb)) + path;
        return false;
    }
    ReadResult r = read_pxb_file(path);
    if (!r.ok()) {
        status_msg = std::string(tr(Str::StatusFailed)) +
                     (r.error.empty() ? tr(Str::StatusUnknownErr) : r.error);
        return false;
    }
    doc = std::move(r.doc);
    current_path = path;
    // Force a re-scan of the file's directory. refresh_siblings() normally
    // short-circuits when the directory is unchanged; clearing the cached dir
    // guarantees the sibling list always reflects the newly opened file's
    // folder, even when it happens to equal the startup cwd (where a failed
    // or empty first scan would otherwise leave the list stale forever).
    siblings_dir_.clear();
    refresh_siblings();
    reset_document_state();
    char buf[512];
    snprintf(buf, sizeof(buf), tr(Str::StatusLoadedFmt),
             path_filename(path).c_str(), frame_count(), (int)doc.layers.size());
    status_msg = buf;
    if (on_document_loaded) on_document_loaded(doc);
    return true;
}

void App::tick(double dt) {
    if (!playing || frame_count() <= 1) return;
    play_accum += dt;
    // Per-frame time: explicit duration wins, else the global fps default.
    double frame_dur = effective_frame_duration_ms(current_frame) / 1000.0;
    while (play_accum >= frame_dur) {
        play_accum -= frame_dur;
        current_frame = (current_frame + 1) % frame_count();
    }
}

void App::stop_playback() {
    playing = false;
    current_frame = 0;
    play_accum = 0.0;
}

} // namespace pxb
