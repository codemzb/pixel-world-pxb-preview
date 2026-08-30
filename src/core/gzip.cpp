// gzip.cpp
#include "gzip.h"
#include "miniz.h"   // vendored at thirdparty/miniz/miniz.h

#include <cstring>
#include <cstdint>

namespace pxb {

// NOTE: PXB files are gzip-wrapped. We parse the gzip member header ourselves
// to locate the raw DEFLATE stream, then raw-inflate it (-MZ_DEFAULT_WINDOW_BITS).
// This keeps the layer independent of whether the vendored miniz build exposes
// native gzip support, and lets us bounds-check the header strictly.
//
// IMPORTANT (miniz 11.0.2): the incremental loop must use MZ_NO_FLUSH, not
// MZ_FINISH. With MZ_FINISH, once the initial output buffer fills up (inflate
// returns MZ_BUF_ERROR) and we grow the buffer and call again, miniz returns
// MZ_DATA_ERROR instead of continuing -- any file whose decompressed payload
// exceeds the initial buffer (64 KiB) fails to load. MZ_NO_FLUSH continues
// correctly and reports MZ_STREAM_END when the stream is complete.

// Hard cap on decompressed size: 1 GiB. PXB payloads are tiny (tens of KiB);
// this guards against a malicious/corrupt stream forcing unbounded growth.
static const size_t kMaxOut = (size_t)1 << 30;

bool is_gzip(const uint8_t* data, size_t size) {
    return size >= 10 && data[0] == 0x1f && data[1] == 0x8b && data[2] == 0x08;
}

// Parse the gzip member header and return the byte offset of the raw DEFLATE
// stream. Returns 0 on any malformation or truncation.
//
// Every field read is bounds-checked with `i + k <= n` BEFORE advancing, so a
// truncated/crafted header can never read past the buffer end. Exposed (non-
// static) so gzip_partial.cpp can reuse the exact same header-skip logic.
size_t gzip_deflate_offset(const uint8_t* d, size_t n) {
    // 10-byte fixed header: id1 id2 cm flg mtime(4) xfl os
    if (n < 10) return 0;
    uint8_t flg = d[3];
    size_t i = 10;
    if (flg & 0x04) {                 // FEXTRA
        if (i + 2 > n) return 0;      // need 2 bytes for XLEN
        size_t xlen = (size_t)d[i] | ((size_t)d[i + 1] << 8);
        i += 2;
        if (xlen > n - i) return 0;   // extra field must fit in buffer
        i += xlen;
    }
    if (flg & 0x08) {                 // FNAME (NUL-terminated)
        while (i < n && d[i] != 0) ++i;
        if (i >= n) return 0;         // missing terminator
        ++i;                          // skip the NUL
    }
    if (flg & 0x10) {                 // FCOMMENT (NUL-terminated)
        while (i < n && d[i] != 0) ++i;
        if (i >= n) return 0;
        ++i;
    }
    if (flg & 0x02) {                 // FHCRC (2 bytes CRC16)
        if (i + 2 > n) return 0;
        i += 2;
    }
    if (flg & 0x01) {                 // FRESERVED bit set -> invalid per RFC 1952
        return 0;
    }
    return i;
}

std::vector<uint8_t> gzip_decompress(const uint8_t* data, size_t size) {
    std::vector<uint8_t> out;
    if (!is_gzip(data, size)) return out;
    size_t off = gzip_deflate_offset(data, size);
    if (off == 0 || off >= size) return out;

    mz_stream strm;
    memset(&strm, 0, sizeof(strm));
    // Raw DEFLATE (no zlib/gzip wrapper); the 8-byte gzip trailer is left for
    // inflate to ignore (it stops at MZ_STREAM_END).
    int status = mz_inflateInit2(&strm, -MZ_DEFAULT_WINDOW_BITS);
    if (status != MZ_OK) return out;

    strm.next_in = data + off;
    strm.avail_in = (unsigned int)(size - off);

    size_t cap = 1 << 16;          // 64 KiB initial
    out.resize(cap);
    strm.next_out = out.data();
    strm.avail_out = (unsigned int)cap;

    bool done = false;
    while (!done) {
        mz_ulong out_before = strm.total_out;
        status = mz_inflate(&strm, MZ_NO_FLUSH);
        if (status == MZ_STREAM_END) {
            done = true;
        } else if (status == MZ_OK || status == MZ_BUF_ERROR) {
            if (strm.avail_out == 0) {
                // Output exhausted -> grow and continue.
                if (cap >= kMaxOut) {
                    mz_inflateEnd(&strm);
                    out.clear();
                    return out;
                }
                size_t used = cap - strm.avail_out;
                cap *= 2;
                out.resize(cap);
                strm.next_out = out.data() + used;
                strm.avail_out = (unsigned int)(cap - used);
                continue;
            }
            // Output space available but inflate produced nothing: truncated
            // (input exhausted mid-stream) or corrupt stream.
            if (strm.total_out == out_before) {
                mz_inflateEnd(&strm);
                out.clear();
                return out;
            }
            // Progress was made; call again (may be waiting for more input).
        } else {
            mz_inflateEnd(&strm);
            out.clear();
            return out;
        }
    }
    out.resize(cap - strm.avail_out);
    mz_inflateEnd(&strm);
    return out;
}

} // namespace pxb
