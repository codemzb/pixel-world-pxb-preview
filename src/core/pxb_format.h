// pxb_format.h
//
// PXB file format - shared definitions.
//
// The PXB container (observed in files produced by "MZB.ONE") is:
//
//   [ gzip stream ]
//       |
//       v  (zlib/deflate, no FNAME)
//   [ PXB payload ]
//       |  offset  size  field
//       |  ------  -----  ---------------------------------------------
//       |   0      4      magic  ASCII "PXB1"
//       |   4      2      u16 LE  version_major
//       |   6      2      u16 LE  version_minor
//       |   8      8      u64 LE  json_length  (bytes of JSON block)
//       |  16      json_length     UTF-8 JSON metadata
//       |  16+json_length ...      data section
//       |                              preview_table entries reference
//       |                              byte ranges inside this section;
//       |                              each referenced blob is a PNG
//       |                              (signature 89 50 4E 47 ...).
//       |                              Remaining tail holds the editable
//       |                              source data (palette-indexed pixel
//       |                              buffers, frame/layer structure).
//
// All multi-byte integers are LITTLE ENDIAN.
// See docs/pxb-format-spec.md for the full specification.
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pxb {

// File = gzip-compressed payload.
// Payload magic.
static constexpr char kMagic[4] = {'P', 'X', 'B', '1'};
static constexpr uint32_t kMagicU32 = 0x31525850; // "PXB1" little-endian read as u32

// Current reader understands payload version major 2 (minor is forward-tolerant).
static constexpr uint16_t kFormatMajor = 2;

// Preview entry kinds found in JSON `preview_table`.
enum class PreviewKind {
    Unknown,
    Thumbnail,
    Frame,
};

// One entry from JSON `preview_table`.
struct PreviewEntry {
    PreviewKind kind = PreviewKind::Unknown;
    int frame_index = -1;     // valid when kind == Frame
    uint64_t offset = 0;      // byte offset from start of data section
    uint64_t length = 0;      // byte length of the blob
    std::string blob_type;    // raw "type" string from JSON
};

// Scene descriptor parsed from JSON `scene`.
struct SceneInfo {
    std::string dimension;       // raw value, e.g. "2", "2.5", "3" (display
                                 // normalized to "2D"/"2.5D"/"3D" in the UI)
    int width = 0;
    int height = 0;
    std::string color_depth;     // e.g. "8-bit"
    std::string coordinate;      // e.g. "cartesian_top_left"
};

// Document-level metadata.
struct DocumentMeta {
    std::string title;
    std::string generator;
    std::string created_at;
    std::string updated_at;
    std::string palette_id;     // e.g. "pxcolor" (external palette)
    std::string palette_version;
    std::string version;        // format/tool version string
    std::string content_type;
};

// A decoded RGBA image (always 8-bit, 4 channels).
struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // width*height*4, row-major, RGBA

    // An image is "empty" when it has no valid dimensions. We intentionally
    // do NOT require pixels to be non-empty: after a successful GPU upload the
    // CPU-side pixel buffer may be discarded to save RAM, while the dimensions
    // are still needed for layout and the GPU texture remains valid.
    bool empty() const { return width <= 0 || height <= 0; }
    bool has_pixels() const { return !empty() && !pixels.empty(); }
    size_t byte_size() const { return (size_t)width * (size_t)height * 4u; }
};

// Discovered layer (name parsed from the editable source region).
struct LayerInfo {
    std::string name;
    int id = 0;
    bool visible = true;        // user-controlled visibility state
};

// A single composited frame plus its optional display duration.
struct FrameInfo {
    RgbaImage image;       // composited PNG for this frame
    int duration_ms = -1;  // per-frame duration in milliseconds;
                           // -1 = unspecified -> playback falls back to global fps.
                           // (Using -1 rather than 0 keeps a real 0ms frame
                           // distinguishable from "no duration given".)
                           // Forward-compatible: current MZB.ONE exports do not
                           // emit this field, so it is parsed opportunistically.
};

// Fully parsed PXB document.
struct PxbDocument {
    uint16_t version_major = 0;
    uint16_t version_minor = 0;
    DocumentMeta meta;
    SceneInfo scene;
    std::vector<PreviewEntry> previews;
    std::vector<FrameInfo> frame_images;   // one composited PNG per frame (+duration)
    RgbaImage thumbnail;                    // preview_table "thumbnail"
    int thumbnail_preview_index = 0;
    std::vector<LayerInfo> layers;          // names parsed from source region

    bool ok() const { return !frame_images.empty() || !thumbnail.empty(); }
    int frame_count() const { return (int)frame_images.size(); }
};

} // namespace pxb
