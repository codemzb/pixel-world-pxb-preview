// image.h
//
// Decode embedded PNG blobs (from the PXB preview_table) into RGBA32.
// Uses stb_image (single-header, public domain).
//
#pragma once

#include "pxb_format.h"
#include <string>
#include <cstdint>

namespace pxb {

// Decode a memory buffer containing a PNG (or any stb_image-supported format)
// into an RGBA image. Returns an empty RgbaImage on failure.
RgbaImage decode_image_rgba(const uint8_t* data, size_t size);

inline RgbaImage decode_image_rgba(const std::vector<uint8_t>& buf) {
    if (buf.empty()) return {};
    return decode_image_rgba(buf.data(), buf.size());
}

// Read an image from disk by path (UTF-8). Empty RgbaImage on failure.
RgbaImage load_png_file(const std::string& path);

// Nearest-neighbour downscale to a square NxN image. Fast and dependency-free;
// visually fine at small sizes (e.g. the About icon glyph). Used by the UI
// layer; the Windows thumbnail DLL and the Linux thumbnailer keep their own
// aspect-ratio-preserving variants because their scaling semantics differ.
RgbaImage resize_nearest(const RgbaImage& src, int n);

// Does the buffer start with the PNG signature?
bool is_png(const uint8_t* data, size_t size);

} // namespace pxb
