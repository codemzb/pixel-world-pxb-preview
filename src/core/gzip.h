// gzip.h
//
// gzip decompression helper. Uses miniz (single-file, public domain).
// PXB files are gzip (deflate) streams; miniz's zlib-compatible inflater with
// windowBits = 16 + MAX_WBITS auto-detects the gzip wrapper.
//
#pragma once

#include <cstdint>
#include <vector>

namespace pxb {

// Decompress a gzip/zlib buffer into raw bytes.
// Returns empty vector on failure. Never throws.
std::vector<uint8_t> gzip_decompress(const uint8_t* data, size_t size);

inline std::vector<uint8_t> gzip_decompress(const std::vector<uint8_t>& in) {
    if (in.empty()) return {};
    return gzip_decompress(in.data(), in.size());
}

// Quick check: does this buffer begin with the gzip magic (1f 8b 08)?
bool is_gzip(const uint8_t* data, size_t size);

// Parse the gzip member header and return the byte offset of the raw DEFLATE
// stream, or 0 on any malformation / truncation. Single source of truth for the
// header-skip logic, shared by the one-shot decompressor (gzip_decompress) and
// the incremental GzipInflater (gzip_partial.cpp). Every field read is
// bounds-checked so a crafted/truncated header can never read past the buffer.
size_t gzip_deflate_offset(const uint8_t* data, size_t size);

} // namespace pxb
