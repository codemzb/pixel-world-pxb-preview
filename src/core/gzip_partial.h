// gzip_partial.h
//
// Incremental (streaming) gzip inflater. Unlike gzip_decompress() which returns
// the *entire* decompressed payload at once, this class lets a caller decompress
// only as much of the stream as it actually needs — e.g. just enough bytes to
// reach the JSON metadata block at the front of a PXB payload, or just far
// enough to extract one embedded thumbnail PNG. This is the enabling primitive
// for lazy / partial decompression (see pxb_meta.cpp and the design doc
// DECOMPRESS_DESIGN_2026-08-25.md).
//
// Backed by miniz's zlib-compatible inflater (raw DEFLATE, -MZ_DEFAULT_WINDOW_BITS),
// reusing the same header-skip logic as gzip_decompress() via gzip_deflate_offset().
//
#pragma once

#include <cstdint>
#include <vector>
#include "miniz.h"   // mz_stream / mz_inflate* (vendored single-header)

namespace pxb {

class GzipInflater {
public:
    // max_out bounds the decompressed size (guards against malicious/corrupt
    // streams). The default is 1 GiB (matches the one-shot decompressor); callers
    // that only need a metadata prefix can pass a much smaller budget.
    explicit GzipInflater(size_t max_out = (size_t)1 << 30);
    ~GzipInflater();

    // Parse the gzip header and set up the raw-deflate stream. Returns false if
    // the input is not a well-formed gzip stream.
    bool init(const uint8_t* data, size_t size);

    // Inflate until out().size() >= wanted OR the stream ends. Returns true if
    // no error occurred (note: returns true even if the stream ended before
    // `wanted` bytes were produced — callers must check out_size() >= wanted to
    // detect a truncated stream).
    bool inflate_until(size_t wanted);

    // Continue inflating until at least `extra` MORE output bytes are produced or
    // the stream ends. Used to walk forward to a later blob.
    bool inflate_more(size_t extra = (1u << 16));

    bool finished() const { return finished_; }
    bool failed() const { return failed_; }
    const std::vector<uint8_t>& out() const { return out_; }
    size_t out_size() const { return out_.size(); }

private:
    // Shared inflate loop: keep pulling until out_.size() >= target or the stream
    // ends / errors. Respects max_out by growing the output buffer (doubling,
    // capped) exactly like gzip_decompress(). Returns false on hard failure.
    bool pump(size_t target);

    const uint8_t* data_ = nullptr;
    size_t data_size_ = 0;
    mz_stream* strm_ = nullptr;          // miniz inflater state (allocated on the heap)
    bool inited_ = false;
    bool finished_ = false;
    bool failed_ = false;
    std::vector<uint8_t> out_;
    size_t cap_ = 0;
    size_t max_out_;
};

} // namespace pxb
