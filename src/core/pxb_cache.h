// pxb_cache.h
//
// Per-file metadata + thumbnail cache, keyed by a CONTENT fingerprint so the same
// file (even renamed/moved) is served from cache without any decompression, and
// any edit forces a re-decompress. List display reads exclusively from this cache
// (never decompresses frames); full frame decompression still happens only in
// pxb_reader::read_pxb when the user previews a file.
//
// Thread-safety: all public methods are safe to call from multiple threads
// (e.g. a background prefetch pool + the main UI thread). Entries are handed
// out as shared_ptr, so a concurrent eviction/mtime-invalidation erase can
// never invalidate a pointer the caller is still holding — the entry stays
// alive until the last user drops it. GL texture deletion is deliberately NOT
// done here — evicted/deleted entries' GL keys are collected via
// collect_dropped_textures() and deleted by the main thread, because OpenGL
// contexts are per-thread and must never be touched from a worker.
//
#pragma once

#include "pxb_format.h"   // RgbaImage
#include "pxb_meta.h"     // PxbMeta

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <list>
#include <mutex>
#include <cstdint>

namespace pxb {

// One cached entry. `thumb` is always resized to a small square (~88px) so the
// cache memory footprint stays bounded regardless of source image resolution.
struct CachedMeta {
    PxbMeta meta;
    RgbaImage thumb;          // small (resized) thumbnail; pixels may be cleared
                              // after the main thread uploads it to GL.
    int gl_tex_key = -1;      // GL texture key assigned by MetaCache (negative
                              // namespace, never collides with document keys);
                              // -1 means "not yet uploaded".
    uint64_t file_size = 0;   // weak pre-check: size + mtime cheaply detect edits
    int64_t mtime_ns = 0;     // without re-reading / re-hashing the whole file.
    uint32_t crc32 = 0;       // content hash of the compressed bytes (cache key half)
};

// Composite key = (file_size, crc32). The size half makes a collision require
// both an identical compressed-byte CRC AND an identical file size — effectively
// a ~96-bit key, so accidental collisions are not a practical concern.
using CacheKey = std::pair<uint64_t, uint32_t>;

class MetaCache {
public:
    explicit MetaCache(size_t cap = 512) : cap_(cap) {}
    ~MetaCache() = default;

    // Non-blocking: return the cached entry if present AND the file's (size, mtime)
    // still match; otherwise return nullptr. Never reads the file. The shared_ptr
    // keeps the entry alive even if another thread erases it right after.
    std::shared_ptr<CachedMeta> find(const std::string& path);

    // Warm an entry on a miss (blocking partial decompress: metadata + one
    // thumbnail). Thread-safe. Returns true when an entry exists afterwards.
    bool prefetch(const std::string& path);

    // Drop every entry, recording all GL keys for later deletion on the main
    // thread. Call when the scanned directory changes.
    void clear();

    // Hand the main thread the GL texture keys that were dropped via eviction so
    // it can delete them. Swaps the internal pending list (cheap).
    void collect_dropped_textures(std::vector<int>& out);

    size_t size() const { return map_.size(); }
    size_t cap() const { return cap_; }
    // Approximate resident memory: sum of thumbnail bytes + per-entry overhead.
    size_t mem_bytes() const;

private:
    // Perform the (blocking) partial read + insert. Assumes the entry is NOT
    // already present (callers check). Safe under concurrent calls.
    std::shared_ptr<CachedMeta> do_load(const std::string& path);
    void erase_key(CacheKey k);

    mutable std::mutex mu_;
    std::map<CacheKey, std::shared_ptr<CachedMeta>> map_;
    std::list<CacheKey> lru_;                       // front = oldest
    std::unordered_map<std::string, CacheKey> path_keys_;   // path -> key
    std::map<CacheKey, std::string> key_paths_;             // key -> path
    std::vector<int> pending_drops_;                // GL keys awaiting deletion
    size_t cap_;
    int next_key_ = -1000000;                      // descending negative namespace
};

} // namespace pxb
