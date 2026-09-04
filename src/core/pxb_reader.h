// pxb_reader.h
//
// Loads and parses a .pxb file into a PxbDocument.
// Pipeline: read file -> gzip decompress -> parse payload
//           (magic/version/json_length) -> JSON metadata -> preview PNG blobs
//           -> optional layer-name scan of the editable source region.
//
#pragma once

#include "pxb_format.h"
#include <string>
#include <vector>
#include <cstdint>

namespace pxb {

struct ReadResult {
    PxbDocument doc;
    std::string error;        // empty if ok
    bool ok() const { return error.empty() && doc.ok(); }
};

// Parse an in-memory .pxb file (gzip stream).
ReadResult read_pxb(const std::vector<uint8_t>& file_bytes);

// Read a .pxb from disk.
ReadResult read_pxb_file(const std::string& path);

// Convenience: load just the thumbnail (used by shell thumbnailers/QuickLook).
// Returns an empty image on failure. `path` is a filesystem path.
RgbaImage read_thumbnail_file(const std::string& path);

// Memory variants (no filesystem access) - used by shell thumbnail handlers
// that hand us an IStream / file descriptor rather than a path. `thumb_n` is
// the square canvas edge in px — the shell requests up to 256, so decode at
// 256 rather than the app-list size (88) to avoid upscale blur in Explorer.
RgbaImage read_thumbnail_memory(const std::vector<uint8_t>& file_bytes,
                                int thumb_n = 256);

} // namespace pxb
