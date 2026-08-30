// pxb_meta.cpp
#include "pxb_meta.h"
#include "gzip_partial.h"
#include "image.h"

#include <cstring>
#include <string>
#include <cstdint>

namespace pxb {

namespace {
// Read the 16-byte payload header (magic already checked by caller).
inline void read_header16(const uint8_t* p, uint16_t& vmaj, uint16_t& vmin, uint64_t& json_len) {
    memcpy(&vmaj, p + 4, 2);
    memcpy(&vmin, p + 6, 2);
    memcpy(&json_len, p + 8, 8);
}

inline int kind_id(const std::string& bt) {
    if (bt == "thumbnail") return 0;
    if (bt == "frame") return 1;
    return -1;
}
} // namespace

bool parse_pxb_header(const std::vector<uint8_t>& payload, PxbHeader& h) {
    h = PxbHeader{};
    if (payload.size() < 16) { h.error = "payload too small"; return false; }
    if (memcmp(payload.data(), kMagic, 4) != 0) { h.error = "bad magic"; return false; }

    uint16_t vmaj = 0, vmin = 0;
    uint64_t json_len = 0;
    read_header16(payload.data(), vmaj, vmin, json_len);
    h.version_major = vmaj;
    h.version_minor = vmin;
    h.meta.version = std::to_string(vmaj) + "." + std::to_string(vmin);

    if (16 + json_len > payload.size()) { h.error = "json length out of range"; return false; }
    std::string json_text((const char*)&payload[16], (size_t)json_len);

    std::string jerr;
    auto root = json::Parser::parse(json_text, &jerr);
    if (!root) { h.error = "JSON parse error: " + jerr; return false; }
    h.root = root;

    auto& m = h.meta;
    m.title = root->get("title");
    m.generator = root->get("generator");
    m.content_type = root->get("content_type");
    // Keep the "vmaj.vmin" container default unless the JSON carries its own
    // version string (same empty-fallback pattern as generator/title below —
    // an unconditional overwrite used to blank the field on files whose JSON
    // has no top-level "version").
    std::string json_version = root->get("version");
    if (!json_version.empty()) m.version = json_version;
    const auto& meta = (*root)["metadata"];
    if (meta) {
        m.created_at = meta->get("created_at");
        m.updated_at = meta->get("updated_at");
        m.palette_id = meta->get("palette_id");
        m.palette_version = meta->get("palette_version");
        m.generator = meta->get("generator").empty() ? m.generator : meta->get("generator");
        m.title = meta->get("title").empty() ? m.title : meta->get("title");
    }
    const auto& scene = (*root)["scene"];
    if (scene) {
        const auto& dv = (*scene)["dimension"];
        if (dv) m.dimension = dv->is_number() ? std::to_string((int)dv->num) : dv->as_string("");
        const auto& sz = (*scene)["size"];
        if (sz) {
            const auto& wv = (*sz)["width"];
            const auto& hv = (*sz)["height"];
            m.width = wv ? wv->as_int(0) : 0;
            m.height = hv ? hv->as_int(0) : 0;
        }
        m.color_depth = scene->get("color_depth");
        m.coordinate = scene->get("coordinate");
    }
    {
        const auto& ti = (*root)["thumbnail_preview_index"];
        m.thumbnail_preview_index = ti ? ti->as_int(0) : 0;
    }

    h.data_start = 16 + (size_t)json_len;

    const auto& pt = (*root)["preview_table"];
    if (!pt || !pt->is_array()) { h.error = "missing preview_table"; return false; }

    for (const auto& entry : pt->arr) {
        if (!entry) continue;
        std::string bt = entry->get("type");
        int kid = kind_id(bt);
        if (kid == 1) {                       // frame
            m.frame_count++;
            int dur = -1;
            const auto& di = (*entry)["duration"];
            const auto& ti = di ? di : (*entry)["time"];
            if (ti) { int v = (int)ti->as_number(0); if (v > 0) dur = v; }
            m.durations_ms.push_back(dur);
        }
        const auto& ov = (*entry)["offset"];
        const auto& lv = (*entry)["length"];
        uint64_t off = ov ? (uint64_t)ov->as_number(0) : 0;
        uint64_t len = lv ? (uint64_t)lv->as_number(0) : 0;
        if (kid == 0) {                        // dedicated thumbnail
            h.thumb_off = off; h.thumb_len = len; h.thumb_kind = 0;
        } else if (kid == 1) {                 // frame0 fallback candidate
            auto fi = (*entry)["frame_index"];
            int fi_v = fi ? fi->as_int(-1) : -1;
            if (h.thumb_kind < 0 || fi_v == 0) { h.frame0_off = off; h.frame0_len = len; }
        }
    }

    h.ok = true;
    return true;
}

PxbMeta read_metadata(const std::vector<uint8_t>& file_bytes) {
    PxbMeta out;
    GzipInflater inf;
    if (!inf.init(file_bytes.data(), file_bytes.size())) { out.error = "not a gzip stream"; return out; }
    // Inflate just the 16-byte header to learn json_len, then the JSON block.
    if (!inf.inflate_until(16)) { out.error = "gzip decompression failed"; return out; }
    uint16_t vmaj = 0, vmin = 0; uint64_t json_len = 0;
    read_header16(inf.out().data(), vmaj, vmin, json_len);
    if (!inf.inflate_until(16 + (size_t)json_len)) { out.error = "gzip decompression failed"; return out; }

    PxbHeader h;
    if (!parse_pxb_header(inf.out(), h)) { out.error = h.error; return out; }
    out = h.meta;
    out.ok = true;
    return out;
}

bool read_meta_and_thumb(const std::vector<uint8_t>& file_bytes,
                         PxbMeta& meta_out, RgbaImage& thumb_out, int thumb_n) {
    GzipInflater inf;
    if (!inf.init(file_bytes.data(), file_bytes.size())) return false;
    if (!inf.inflate_until(16)) return false;
    uint16_t vmaj = 0, vmin = 0; uint64_t json_len = 0;
    read_header16(inf.out().data(), vmaj, vmin, json_len);
    if (!inf.inflate_until(16 + (size_t)json_len)) return false;

    PxbHeader h;
    if (!parse_pxb_header(inf.out(), h)) return false;
    meta_out = h.meta;
    meta_out.ok = true;

    // Choose which single blob to extract: dedicated thumbnail, else frame0.
    uint64_t blob_off = 0, blob_len = 0;
    if (h.thumb_kind == 0) { blob_off = h.thumb_off; blob_len = h.thumb_len; }
    else if (h.frame0_len > 0) { blob_off = h.frame0_off; blob_len = h.frame0_len; }
    else return true;   // no preview image available; metadata only

    size_t need = h.data_start + (size_t)blob_off + (size_t)blob_len;
    if (!inf.inflate_until(need) || inf.out_size() < need) return false;

    const uint8_t* blob = inf.out().data() + h.data_start + (size_t)blob_off;
    RgbaImage img = decode_image_rgba(blob, (size_t)blob_len);
    if (img.empty()) return false;
    thumb_out = resize_nearest(img, thumb_n);
    return true;
}

RgbaImage read_thumbnail_partial(const std::vector<uint8_t>& file_bytes, int thumb_n) {
    PxbMeta m;
    RgbaImage thumb;
    if (!read_meta_and_thumb(file_bytes, m, thumb, thumb_n)) return {};
    return thumb;
}

} // namespace pxb
