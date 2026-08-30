// gzip_partial.cpp
#include "gzip_partial.h"
#include "gzip.h"
#include "miniz.h"

#include <cstring>
#include <new>
#include <cstdint>

namespace pxb {

GzipInflater::GzipInflater(size_t max_out) : max_out_(max_out) {}

GzipInflater::~GzipInflater() {
    if (inited_ && strm_) mz_inflateEnd(strm_);
    delete strm_;
}

bool GzipInflater::init(const uint8_t* data, size_t size) {
    if (!is_gzip(data, size)) return false;
    size_t off = gzip_deflate_offset(data, size);
    if (off == 0 || off >= size) return false;

    if (!strm_) strm_ = new (std::nothrow) mz_stream();
    if (!strm_) return false;
    memset(strm_, 0, sizeof(*strm_));

    // Raw DEFLATE (no zlib/gzip wrapper); the gzip trailer is left for the caller
    // to verify (see verify_gzip_trailer in pxb_reader.cpp). MZ_NO_FLUSH is
    // mandatory for the incremental loop (see gzip.cpp for the MZ_FINISH caveat).
    int status = mz_inflateInit2(strm_, -MZ_DEFAULT_WINDOW_BITS);
    if (status != MZ_OK) { delete strm_; strm_ = nullptr; return false; }
    inited_ = true;

    data_ = data;
    data_size_ = size;
    strm_->next_in = const_cast<uint8_t*>(data + off);
    strm_->avail_in = (unsigned int)(size - off);

    cap_ = 1 << 16;                 // 64 KiB initial
    out_.resize(cap_);
    strm_->next_out = out_.data();
    strm_->avail_out = (unsigned int)cap_;
    return true;
}

bool GzipInflater::pump(size_t target) {
    if (failed_ || !strm_) return false;

    // NOTE: we must track the produced byte count via strm_->total_out, NOT
    // out_.size(). init() reserves `cap_` bytes in out_ (so out_.size() is the
    // buffer capacity, not the amount decoded). Using out_.size() here would make
    // the loop condition `out_.size() < target` false on the very first call
    // whenever target <= cap_, so inflate would never run and the final
    // out_.resize(total_out) below would truncate the buffer to 0 bytes.
    while ((size_t)strm_->total_out < target && !finished_) {
        if (strm_->avail_out == 0) {
            // Output buffer exhausted: grow (doubling, capped at max_out).
            // `used` is the bytes already produced (total_out), which remain
            // valid in the front of the buffer; we continue writing after them.
            if (cap_ >= max_out_) { failed_ = true; mz_inflateEnd(strm_); inited_ = false; return false; }
            size_t used = (size_t)strm_->total_out;
            cap_ *= 2;
            if (cap_ > max_out_) cap_ = max_out_;
            out_.resize(cap_);
            strm_->next_out = out_.data() + used;
            strm_->avail_out = (unsigned int)(cap_ - used);
        }

        size_t before = (size_t)strm_->total_out;
        int status = mz_inflate(strm_, MZ_NO_FLUSH);
        if (status == MZ_STREAM_END) {
            finished_ = true;
            break;
        } else if (status == MZ_OK || status == MZ_BUF_ERROR) {
            if (strm_->avail_out == 0) continue;   // grew above; pull again
            // No progress and no output space: truncated / corrupt stream.
            if ((size_t)strm_->total_out == before) {
                failed_ = true; mz_inflateEnd(strm_); inited_ = false; return false;
            }
        } else {
            failed_ = true; mz_inflateEnd(strm_); inited_ = false; return false;
        }
    }

    out_.resize((size_t)strm_->total_out);
    return !failed_;
}

bool GzipInflater::inflate_until(size_t wanted) {
    // pump() leaves out_ holding everything decoded so far (possibly more than
    // `wanted` if one inflate call filled the buffer). We have enough once the
    // produced size reaches `wanted`; the stream finishing early just means we
    // return whatever is available and the caller checks out_size() if needed.
    bool ok = pump(wanted);
    return ok && out_.size() >= wanted;
}

bool GzipInflater::inflate_more(size_t extra) {
    return pump(out_.size() + extra);
}

} // namespace pxb
