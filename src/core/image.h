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

// Decode a "data:image/<type>;base64,<payload>" URI (as embedded in some PXB
// metadata variants, e.g. metadata.thumbnail_url) into an RGBA image. Returns
// an empty RgbaImage for unsupported URIs / decode failure.
RgbaImage decode_data_uri_rgba(const std::string& uri);

// Read an image from disk by path (UTF-8). Empty RgbaImage on failure.
RgbaImage load_png_file(const std::string& path);

// Nearest-neighbour downscale to a square NxN image. Fast and dependency-free;
// visually fine at small sizes (e.g. the About icon glyph). Used by the UI
// layer; the Windows thumbnail DLL and the Linux thumbnailer keep their own
// aspect-ratio-preserving variants because their scaling semantics differ.
RgbaImage resize_nearest(const RgbaImage& src, int n);

// Nearest-neighbour scale of `src` onto an NxN transparent canvas WITHOUT
// distorting the image: the art keeps its aspect ratio, is sized so its larger
// side hits N, and is centered. Square sources produce a 1:1 copy. This is the
// shape thumbnails must have — a square canvas with the image letterboxed
// inside — so rectangular pixel art never gets squeezed into a square by the
// app's list or the shell thumbnail handlers.
RgbaImage fit_square_nearest(const RgbaImage& src, int n);

// Does the buffer start with the PNG signature?
bool is_png(const uint8_t* data, size_t size);

} // namespace pxb
