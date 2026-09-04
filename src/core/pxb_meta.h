// pxb_meta.h
//
// Partial / lazy reading of PXB files. A .pxb is a gzip stream whose payload
// starts with a 16-byte header, then a JSON metadata block, then a data section
// of PNG blobs referenced by the JSON `preview_table`. Because the metadata
// lives at the FRONT of the stream, we can incrementally inflate just far enough
// to read it (and, if needed, one thumbnail PNG) WITHOUT decoding any animation
// frame. This is what lets the file-list UI show titles + thumbnails while
// deferring the expensive full (all-frames) decompression until the user actually
// previews a file.
//
#pragma once

#include "pxb_format.h"   // RgbaImage, PxbDocument, PreviewKind
#include "image.h"        // RgbaImage
#include "json_min.h"     // json::ValuePtr

#include <string>
#include <vector>
#include <cstdint>

namespace pxb {

// Metadata extracted from the JSON header (no frames decoded).
struct PxbMeta {
    bool ok = false;
    std::string error;

    std::string title;
    std::string generator;
    std::string created_at;
    std::string updated_at;
    std::string palette_id;
    std::string palette_version;
    std::string version;
    std::string content_type;

    std::string dimension;       // raw scene.dimension (e.g. "2", "2.5", "3")
    int width = 0;
    int height = 0;
    std::string color_depth;
    std::string coordinate;

    int thumbnail_preview_index = 0;
    int frame_count = 0;
    std::vector<int> durations_ms;   // per-frame duration (-1 = unspecified)

    // `metadata.thumbnail_url` when it is a data: URI (newer exports that have
    // no preview_table carry the document image here). Kept raw (not decoded)
    // so the lazy list path only pays a PNG decode when a thumbnail is wanted.
    std::string thumbnail_data_uri;
};

// Parsed header for both the lazy and full paths. `root` is kept so callers can
// iterate `preview_table` for frame blobs without re-parsing the JSON.
struct PxbHeader {
    bool ok = false;
    std::string error;

    PxbMeta meta;
    size_t data_start = 0;          // = 16 + json_len
    uint16_t version_major = 0;
    uint16_t version_minor = 0;
    json::ValuePtr root;            // parsed JSON (for preview_table iteration)

    // Embedded thumbnail blob location (relative to data_start). thumb_kind:
    // 0 = explicit "thumbnail" preview, 1 = frame0 fallback, -1 = none.
    uint64_t thumb_off = 0, thumb_len = 0;
    int thumb_kind = -1;
    // frame0 blob location (fallback when no dedicated thumbnail exists).
    uint64_t frame0_off = 0, frame0_len = 0;
};

// Parse the 16-byte header + JSON metadata from a (fully or partially) available
// payload prefix. Fills `meta`, `data_start`, `root`, and the thumbnail/frame0
// blob locations. Returns false on malformed input / unparseable JSON.
bool parse_pxb_header(const std::vector<uint8_t>& payload, PxbHeader& h);

// Lazy metadata only: inflate just the JSON prefix and return metadata. Does NOT
// decode any frame or thumbnail image.
PxbMeta read_metadata(const std::vector<uint8_t>& file_bytes);

// Lazy metadata + one thumbnail: inflate just far enough to reach the embedded
// thumbnail (or frame0 fallback), decode that single PNG, and resize it to a
// small square (thumb_n x thumb_n) so the cache stays memory-bounded. On
// success `meta_out`/`thumb_out` are filled and true is returned.
bool read_meta_and_thumb(const std::vector<uint8_t>& file_bytes,
                         PxbMeta& meta_out, RgbaImage& thumb_out, int thumb_n = 88);

// Convenience: just the thumbnail (used by shell thumbnailers / QuickLook).
RgbaImage read_thumbnail_partial(const std::vector<uint8_t>& file_bytes, int thumb_n = 88);

} // namespace pxb
