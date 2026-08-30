// pxb_cache.cpp
#include "pxb_cache.h"
#include "pxb_meta.h"      // read_meta_and_thumb
#include "fileutil.h"      // read_file_bytes, file_stat
#include "miniz.h"         // mz_crc32
// miniz.h does `#define crc32 mz_crc32`, which would rewrite the CachedMeta::crc32
// member access below into `c.mz_crc32` and break the build. We only ever call
// `mz_crc32` explicitly, so undefine the alias right after the include.
#undef crc32

#include <algorithm>
#include <mutex>
#include <cstdint>
#include <memory>

namespace pxb {

std::shared_ptr<CachedMeta> MetaCache::find(const std::string& path) {
    std::lock_guard<std::mutex> lk(mu_);
    auto pit = path_keys_.find(path);
    if (pit == path_keys_.end()) return nullptr;
    CacheKey k = pit->second;
    auto it = map_.find(k);
    if (it == map_.end()) {
        // Stale forward mapping (the entry vanished); clean both directions so
        // the orphaned key_paths_ entry can't erase a future path -> key link.
        path_keys_.erase(pit);
        key_paths_.erase(k);
        return nullptr;
    }

    // Weak pre-check: if size or mtime changed, treat as a different file.
    int64_t mt = 0; uint64_t sz = 0;
    if (file_stat(path, &mt, &sz) &&
        (it->second->file_size != sz || it->second->mtime_ns != mt)) {
        erase_key(k);
        return nullptr;
    }
    // Touch LRU.
    lru_.remove(k);
    lru_.push_back(k);
    // Copy the shared_ptr out; the entry outlives the lock for every holder.
    return it->second;
}

std::shared_ptr<CachedMeta> MetaCache::do_load(const std::string& path) {
    std::string err;
    std::vector<uint8_t> buf = read_file_bytes(path, &err);
    if (buf.empty()) return nullptr;

    uint32_t crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, buf.data(), buf.size());
    int64_t mt = 0; uint64_t sz = 0;
    if (!file_stat(path, &mt, &sz)) { sz = buf.size(); mt = 0; }

    PxbMeta meta;
    RgbaImage thumb;
    bool ok = read_meta_and_thumb(buf, meta, thumb, 88);
    if (!ok && meta.error.empty()) meta.error = "partial read failed";

    std::lock_guard<std::mutex> lk(mu_);
    CacheKey key{sz, crc};
    // Another thread may have loaded it while we were decoding.
    auto existing = map_.find(key);
    if (existing != map_.end()) {
        lru_.remove(key); lru_.push_back(key);
        // Record this path's mapping too (the file may have been renamed /
        // copied since the first load: same content, new path).
        path_keys_[path] = key;
        return existing->second;
    }
    // Evict until we have room.
    while (map_.size() >= cap_) {
        if (lru_.empty()) break;
        erase_key(lru_.front());
    }
    auto entry = std::make_shared<CachedMeta>();
    entry->meta = std::move(meta);
    entry->thumb = std::move(thumb);
    entry->file_size = sz;
    entry->mtime_ns = mt;
    entry->crc32 = crc;
    entry->gl_tex_key = next_key_--;
    map_[key] = entry;
    // If this path was cached under a different content key before (the file
    // changed between find() and do_load), drop the stale reverse mapping so
    // evicting the old key later won't erase the new path -> key link.
    auto pit = path_keys_.find(path);
    if (pit != path_keys_.end()) {
        if (pit->second != key) key_paths_.erase(pit->second);
        path_keys_.erase(pit);
    }
    path_keys_[path] = key;
    key_paths_[key] = path;
    lru_.push_back(key);
    return entry;
}

bool MetaCache::prefetch(const std::string& path) {
    auto c = find(path);
    if (c) return true;
    return do_load(path) != nullptr;
}

void MetaCache::erase_key(CacheKey k) {
    auto it = map_.find(k);
    if (it == map_.end()) return;
    if (it->second->gl_tex_key != -1) pending_drops_.push_back(it->second->gl_tex_key);
    auto kp = key_paths_.find(k);
    if (kp != key_paths_.end()) {
        path_keys_.erase(kp->second);
        key_paths_.erase(kp);
    }
    lru_.remove(k);
    map_.erase(it);
}

void MetaCache::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    while (!lru_.empty()) erase_key(lru_.front());
}

void MetaCache::collect_dropped_textures(std::vector<int>& out) {
    std::lock_guard<std::mutex> lk(mu_);
    if (pending_drops_.empty()) return;
    out.swap(pending_drops_);
    pending_drops_.clear();
}

size_t MetaCache::mem_bytes() const {
    std::lock_guard<std::mutex> lk(mu_);
    size_t total = map_.size() * 256;   // rough per-entry overhead
    for (const auto& kv : map_) total += kv.second->thumb.byte_size();
    return total;
}

} // namespace pxb
