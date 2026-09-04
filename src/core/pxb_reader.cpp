// pxb_reader.cpp
#include "pxb_reader.h"
#include "gzip.h"
#include "gzip_partial.h"
#include "image.h"
#include "json_min.h"
#include "fileutil.h"   // read_file_bytes — single source for UTF-8 file reads
#include "pxb_meta.h"   // parse_pxb_header, partial reads
#include "miniz.h"      // mz_crc32 — gzip trailer integrity check

#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <cstdint>
#endif

namespace pxb {

static PreviewKind kind_from_string(const std::string& s) {
    if (s == "thumbnail") return PreviewKind::Thumbnail;
    if (s == "frame") return PreviewKind::Frame;
    return PreviewKind::Unknown;
}

// Scan the editable "source" region (everything after the preview_table blobs)
// for length-prefixed UTF-8 names of the form "Frame N" / "Layer N".
// Returns (frame_names, layer_names) with duplicates removed, in file order.
//
// The old implementation ran std::regex_match on every candidate, which both
// heap-allocated a std::string and compiled/executed a regex for essentially
// every byte of the region — vast overkill for a mostly-non-matching buffer.
// We now do a single memcmp prefix test per candidate (one comparison, no
// allocation) and only build the std::string + validate the trailing index for
// the handful of names that actually begin with "Frame"/"Layer". This also
// lets us drop the <regex> dependency entirely.
static void scan_source_names(const std::vector<uint8_t>& payload,
                              size_t source_off,
                              std::vector<std::string>& frames,
                              std::vector<std::string>& layers) {
    if (source_off >= payload.size()) return;
    const auto& r = payload;
    std::set<std::string> seen_f, seen_l;
    for (size_t i = source_off; i + 1 < r.size(); ++i) {
        uint8_t L = r[i];
        // A valid "Frame N"/"Layer N" name is at least the 5-char keyword plus
        // a 1-digit index; cap at 48 as before.
        if (L < 6 || L > 48 || i + 1 + L > r.size()) continue;
        const char* p = (const char*)&r[i + 1];
        bool is_frame = (memcmp(p, "Frame", 5) == 0);
        bool is_layer = (memcmp(p, "Layer", 5) == 0);
        if (!is_frame && !is_layer) continue;
        // Validate the suffix is an index (`\s*\d+`), preserving the old
        // `^(Frame|Layer)\s*\d+` semantics without a regex.
        const char* q = p + 5;
        size_t rem = (size_t)L - 5;
        size_t k = 0;
        while (k < rem && (q[k] == ' ' || q[k] == '\t')) ++k;
        bool ok = (k < rem);
        for (; k < rem && ok; ++k) {
            if (q[k] < '0' || q[k] > '9') { ok = false; break; }
        }
        if (!ok) continue;
        std::string s(p, L);
        if (is_frame) { if (seen_f.insert(s).second) frames.push_back(s); }
        else          { if (seen_l.insert(s).second) layers.push_back(s); }
    }
}

// Verify the gzip trailer (CRC32 + ISIZE) against the fully-decompressed payload.
// The incremental inflater runs in raw-DEFLATE mode and therefore does NOT check
// the gzip trailer on its own; this closes that gap so corrupted downloads /
// truncated files are rejected with a clear error instead of rendering garbage.
static bool verify_gzip_trailer(const std::vector<uint8_t>& file_bytes,
                               const std::vector<uint8_t>& payload) {
    if (file_bytes.size() < 18) return false;   // 10-byte header min + 8-byte trailer
    size_t t = file_bytes.size() - 8;
    auto le32 = [&](size_t o) {
        return (uint32_t)file_bytes[o] | ((uint32_t)file_bytes[o + 1] << 8) |
               ((uint32_t)file_bytes[o + 2] << 16) | ((uint32_t)file_bytes[o + 3] << 24);
    };
    uint32_t crc = le32(t);
    uint32_t isize = le32(t + 4);
    uint32_t calc = (uint32_t)mz_crc32(MZ_CRC32_INIT, payload.data(), payload.size());
    uint32_t calc_isize = (uint32_t)(payload.size() & 0xffffffffULL);
    return calc == crc && calc_isize == isize;
}

// Pull the value of a single string-valued JSON key out of a raw buffer, e.g.
// {"preview_data_url":"data:image/png;base64,..."} . Data URIs contain no
// quotes or escapes, so a plain scan to the closing quote is sufficient; the
// result is validated downstream by the data-URI/PNG decoders anyway.
static bool extract_json_string_field(const uint8_t* buf, size_t len,
                                      const char* key, std::string& out) {
    const size_t klen = strlen(key);
    if (len < klen + 4) return false;
    for (size_t i = 0; i + klen + 2 < len; ++i) {
        if (buf[i] != '"' || memcmp(buf + i + 1, key, klen) != 0) continue;
        size_t j = i + 1 + klen;
        if (j >= len || buf[j] != '"') continue;         // closing quote of key
        while (j < len && buf[j] != ':') ++j;
        while (j < len && buf[j] != '"') ++j;            // opening quote of value
        if (j >= len) return false;
        size_t start = ++j;
        while (j < len && buf[j] != '"') ++j;
        if (j >= len) return false;
        out.assign((const char*)buf + start, j - start);
        return true;
    }
    return false;
}

// Newer exports without a preview_table embed per-frame and per-layer preview
// images in the editable source region, one
// {"preview_data_url":"data:image/...;base64,..."} record per length-prefixed
// "Frame N" / "Layer N" name marker (the same marker shape scan_source_names
// matches). Each record owns everything up to the next same-kind marker;
// entries that were never rendered carry an empty record and are skipped.
// Returns previews ordered by the parsed index.
static std::vector<std::pair<std::string, RgbaImage>>
extract_record_previews(const std::vector<uint8_t>& payload, size_t source_off,
                        const char* keyword) {
    std::vector<std::pair<std::string, RgbaImage>> out;
    const size_t klen = strlen(keyword);   // e.g. 6 for "Frame "/"Layer "
    if (source_off >= payload.size() || klen < 6) return out;
    const auto& r = payload;

    // Locate markers: (record start = length-byte position, name end, parsed
    // index, full name).
    struct Mark { size_t rec_start, name_end; int idx; std::string name; };
    std::vector<Mark> marks;
    for (size_t i = source_off; i + 1 < r.size(); ++i) {
        uint8_t L = r[i];
        if (L < 7 || L > 48 || i + 1 + L > r.size()) continue;
        const char* p = (const char*)&r[i + 1];
        if (memcmp(p, keyword, klen) != 0) continue;
        const char* q = p + klen;
        size_t rem = (size_t)L - klen;
        int idx = 0; bool ok = rem > 0;
        for (size_t k = 0; k < rem && ok; ++k) {
            if (q[k] < '0' || q[k] > '9') { ok = false; break; }
            idx = idx * 10 + (q[k] - '0');
        }
        if (ok) marks.push_back({i, i + 1 + L, idx, std::string(p, L)});
    }
    if (marks.empty()) return out;

    // First preview_data_url record inside each marker's region wins.
    std::map<int, std::pair<std::string, RgbaImage>> by_index;
    for (size_t m = 0; m < marks.size(); ++m) {
        size_t start = marks[m].name_end;
        size_t end = (m + 1 < marks.size()) ? marks[m + 1].rec_start : r.size();
        if (end <= start) continue;
        // Don't reach across a following record of the OTHER kind (a frame's
        // layers sit between it and the next frame marker; stop at either).
        for (size_t k = start; k + 7 < end; ++k) {
            uint8_t Lb = r[k];
            if (Lb >= 6 && Lb <= 48 && k + 1 + Lb <= end &&
                (memcmp(&r[k + 1], "Frame ", 6) == 0 ||
                 memcmp(&r[k + 1], "Layer ", 6) == 0)) { end = k; break; }
        }
        std::string uri;
        if (!extract_json_string_field(r.data() + start, end - start,
                                       "preview_data_url", uri))
            continue;   // empty {} record (never rendered)
        RgbaImage img = decode_data_uri_rgba(uri);
        if (!img.empty())
            by_index.emplace(marks[m].idx,
                             std::make_pair(marks[m].name, std::move(img)));
    }
    for (auto& kv : by_index) out.push_back(std::move(kv.second));
    return out;
}

// Parse an already-decompressed payload into a PxbDocument. Shared by read_pxb
// (full load) — uses parse_pxb_header() for the metadata block so the JSON
// parsing logic lives in exactly one place with the lazy path.
static bool parse_pxb_payload(const std::vector<uint8_t>& payload, ReadResult& res) {
    PxbHeader h;
    if (!parse_pxb_header(payload, h)) { res.error = h.error; return false; }
    auto& d = res.doc;
    d.version_major = h.version_major;
    d.version_minor = h.version_minor;
    d.meta.version = h.meta.version;
    d.meta.title = h.meta.title;
    d.meta.generator = h.meta.generator;
    d.meta.created_at = h.meta.created_at;
    d.meta.updated_at = h.meta.updated_at;
    d.meta.palette_id = h.meta.palette_id;
    d.meta.palette_version = h.meta.palette_version;
    d.meta.content_type = h.meta.content_type;
    d.scene.dimension = h.meta.dimension;
    d.scene.width = h.meta.width;
    d.scene.height = h.meta.height;
    d.scene.color_depth = h.meta.color_depth;
    d.scene.coordinate = h.meta.coordinate;
    d.thumbnail_preview_index = h.meta.thumbnail_preview_index;

    // preview_table is OPTIONAL (see parse_pxb_header): newer exports carry a
    // `texture_table` + inline thumbnail data URI instead. When it is absent
    // (or yields nothing) the fallback at the end of this function renders the
    // document from metadata.thumbnail_url + the source-region frame records.
    size_t data_start = h.data_start;
    struct FrameItem { int frame_index; RgbaImage img; int duration_ms = 0; };
    std::vector<FrameItem> frames;
    uint64_t source_off = data_start;

    // preview_table is OPTIONAL (see parse_pxb_header): newer exports carry a
    // `texture_table` + inline thumbnail data URI instead. When it is absent
    // (or yields nothing) the fallback at the end of this function renders the
    // document from metadata.thumbnail_url + the source-region frame records.
    const auto& pt = (*h.root)["preview_table"];
    if (pt && pt->is_array()) {
        for (const auto& entry : pt->arr) {
            if (!entry) continue;
            PreviewEntry pe;
            pe.blob_type = entry->get("type");
            pe.kind = kind_from_string(pe.blob_type);
            const auto& ov = (*entry)["offset"];
            const auto& lv = (*entry)["length"];
            pe.offset = ov ? (uint64_t)ov->as_number(0) : 0;
            pe.length = lv ? (uint64_t)lv->as_number(0) : 0;
            int dur = -1;
            if (pe.kind == PreviewKind::Frame) {
                auto fi = (*entry)["frame_index"];
                pe.frame_index = fi ? fi->as_int(-1) : -1;
                const auto& di = (*entry)["duration"];
                const auto& ti = di ? di : (*entry)["time"];
                if (ti) {
                    int v = (int)ti->as_number(0);
                    if (v > 0) dur = v;   // only explicit positive durations count
                }
            }
            d.previews.push_back(pe);

            // Bounds-check against the data section; skip if out of range.
            uint64_t avail = (uint64_t)(payload.size() - data_start);
            if (pe.length > avail || pe.offset > avail - pe.length) continue;

            const uint8_t* blob = payload.data() + data_start + (size_t)pe.offset;
            RgbaImage img = decode_image_rgba(blob, (size_t)pe.length);
            if (img.empty()) continue;   // skip undecodable blobs (no empty frames)

            if (pe.kind == PreviewKind::Thumbnail) {
                d.thumbnail = std::move(img);
            } else if (pe.kind == PreviewKind::Frame) {
                frames.push_back({pe.frame_index, std::move(img), dur});
            }
            source_off = std::max(source_off, data_start + (size_t)pe.offset + (size_t)pe.length);
        }

        // Order frames by frame_index (stable for equal indices).
        std::stable_sort(frames.begin(), frames.end(),
                         [](const FrameItem& a, const FrameItem& b) {
                             return a.frame_index < b.frame_index;
                         });
        for (auto& f : frames) d.frame_images.push_back({std::move(f.img), f.duration_ms});
    }

    // Layer / frame name scan from the editable source region.
    std::vector<std::string> frame_names, layer_names;
    scan_source_names(payload, source_off, frame_names, layer_names);
    for (size_t i = 0; i < layer_names.size(); i++) {
        LayerInfo li;
        li.name = layer_names[i];
        li.id = (int)i;
        li.visible = true;
        d.layers.push_back(li);
    }

    if (d.frame_images.empty() && d.thumbnail.empty()) {
        // No usable preview_table. Newer MZB.ONE exports (texture_table
        // flavor) keep the document image in metadata.thumbnail_url and
        // per-frame / per-layer renders as preview_data_url records in the
        // source region.
        d.thumbnail = decode_data_uri_rgba(h.meta.thumbnail_data_uri);
        auto tail_frames = extract_record_previews(payload, data_start, "Frame ");
        if (tail_frames.size() == 1 && !d.thumbnail.empty()) {
            // Single-frame document: the metadata thumbnail shows the same
            // image, usually at a higher resolution — prefer it.
            d.frame_images.push_back({d.thumbnail, -1});
        } else if (!tail_frames.empty()) {
            for (auto& im : tail_frames) d.frame_images.push_back({std::move(im.second), -1});
        } else if (!d.thumbnail.empty()) {
            d.frame_images.push_back({d.thumbnail, -1});
        }
        // Attach isolated layer renders (matched by record name) so the UI
        // can exclude hidden layers from the picture. Layers listed multiple
        // times (e.g. under several frame records) keep their first preview.
        auto tail_layers = extract_record_previews(payload, data_start, "Layer ");
        for (auto& rec : tail_layers) {
            for (auto& ly : d.layers) {
                if (ly.name == rec.first && ly.preview.empty())
                    ly.preview = std::move(rec.second);
            }
        }
    }

    if (d.frame_images.empty() && d.thumbnail.empty()) {
        res.error = "no preview images found in file";
    }
    return res.error.empty();
}

ReadResult read_pxb(const std::vector<uint8_t>& file_bytes) {
    ReadResult res;
    if (file_bytes.size() < 16) { res.error = "file too small"; return res; }

    // Full (eager) decompression — used only when the user actually previews a
    // file. The list/thumbnail path instead uses read_metadata /
    // read_thumbnail_partial, which inflate only as far as they need (see the
    // lazy-decompression design doc).
    GzipInflater inf((size_t)1 << 30);   // 1 GiB hard cap
    if (!inf.init(file_bytes.data(), file_bytes.size())) {
        res.error = "not a gzip stream";
        return res;
    }
    while (!inf.finished() && !inf.failed()) inf.inflate_more();
    if (inf.failed() || inf.out().empty()) {
        res.error = "gzip decompression failed";
        return res;
    }
    // End-to-end integrity: reject corrupt / truncated payloads.
    if (!verify_gzip_trailer(file_bytes, inf.out())) {
        res.error = "gzip integrity check failed (CRC mismatch)";
        return res;
    }
    if (!parse_pxb_payload(inf.out(), res)) {
        if (res.error.empty()) res.error = "parse failed";
    }
    return res;
}

ReadResult read_pxb_file(const std::string& path) {
    // Delegate the UTF-8 path dance + raw read to fileutil (single source of
    // truth for the MinGW locale workaround). Error text is preserved so the
    // UI can show *why* a file failed to load.
    std::string err;
    std::vector<uint8_t> buf = pxb::read_file_bytes(path, &err);
    if (buf.empty()) {
        ReadResult r;
        r.error = err.empty() ? "cannot read file: " + path : err;
        return r;
    }
    return read_pxb(std::move(buf));
}

RgbaImage read_thumbnail_file(const std::string& path) {
    // Lazy partial read: only the embedded thumbnail PNG is decoded (no frames),
    // so the shell thumbnailer / QuickLook never pays for a full-frame inflate.
    std::string err;
    std::vector<uint8_t> buf = pxb::read_file_bytes(path, &err);
    if (buf.empty()) return {};
    return read_thumbnail_partial(buf);
}

RgbaImage read_thumbnail_memory(const std::vector<uint8_t>& file_bytes,
                                int thumb_n) {
    return read_thumbnail_partial(file_bytes, thumb_n);
}

} // namespace pxb
