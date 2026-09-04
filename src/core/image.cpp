// image.cpp
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "image.h"
#include "fileutil.h"

#include <cstring>
#include <fstream>
#include <vector>
#include <cstdint>

namespace pxb {

bool is_png(const uint8_t* data, size_t size) {
    static const unsigned char sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    return size >= 8 && memcmp(data, sig, 8) == 0;
}

RgbaImage decode_image_rgba(const uint8_t* data, size_t size) {
    RgbaImage img;
    if (size < 8 || !is_png(data, size)) return img;

    int w = 0, h = 0, ch = 0;
    // Force 4 channels (RGBA); stb converts automatically.
    stbi_uc* decoded = stbi_load_from_memory(data, (int)size, &w, &h, &ch, 4);
    if (!decoded || w <= 0 || h <= 0) return img;

    img.width = w;
    img.height = h;
    img.pixels.assign(decoded, decoded + (size_t)w * (size_t)h * 4);
    stbi_image_free(decoded);
    return img;
}

RgbaImage load_png_file(const std::string& path) {
    RgbaImage img;
    // read_file_bytes handles UTF-8 on MinGW (CreateFileW under the hood),
    // so Chinese directory names don't mangle the open.
    std::vector<uint8_t> bytes = read_file_bytes(path);
    if (bytes.empty()) return img;
    return decode_image_rgba(bytes);
}

// Standard base64 alphabet; '=' is padding. Returns -1 for invalid chars.
static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

RgbaImage decode_data_uri_rgba(const std::string& uri) {
    RgbaImage img;
    // Only base64 image URIs are useful to us; the pxb samples all use PNG.
    static const char kMarker[] = ";base64,";
    if (uri.compare(0, 5, "data:") != 0) return img;
    size_t marker = uri.find(kMarker, 5);
    if (marker == std::string::npos) return img;
    const char* p = uri.c_str() + marker + sizeof(kMarker) - 1;
    size_t n = uri.size() - (marker + sizeof(kMarker) - 1);

    std::vector<uint8_t> out;
    out.reserve(n / 4 * 3 + 3);
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; ++i) {
        int v = b64_val(p[i]);
        if (v < 0) break;   // '=' padding or trailing garbage: stop
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((acc >> bits) & 0xFF));
        }
    }
    return decode_image_rgba(out);
}

RgbaImage resize_nearest(const RgbaImage& src, int n) {
    RgbaImage out;
    if (src.empty() || n <= 0) return out;
    const int sw = src.width, sh = src.height;
    out.width  = n;
    out.height = n;
    out.pixels.assign((size_t)n * (size_t)n * 4, 0);
    for (int y = 0; y < n; y++) {
        int sy = (int)((double)y * sh / n);
        if (sy >= sh) sy = sh - 1;
        for (int x = 0; x < n; x++) {
            int sx = (int)((double)x * sw / n);
            if (sx >= sw) sx = sw - 1;
            const uint8_t* sp = &src.pixels[(size_t)(sy * sw + sx) * 4];
            uint8_t* dp = &out.pixels[(size_t)(y * n + x) * 4];
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp[3] = sp[3];
        }
    }
    return out;
}

RgbaImage fit_square_nearest(const RgbaImage& src, int n) {
    RgbaImage out;
    if (src.empty() || n <= 0) return out;
    const int sw = src.width, sh = src.height;
    // Scale so the larger side lands on n, then center the result on a
    // transparent NxN canvas (the letterboxed margins stay clear).
    double k = (double)n / (double)(sw > sh ? sw : sh);
    int dw = (int)(sw * k + 0.5);
    int dh = (int)(sh * k + 0.5);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    const int ox = (n - dw) / 2;
    const int oy = (n - dh) / 2;

    out.width  = n;
    out.height = n;
    out.pixels.assign((size_t)n * (size_t)n * 4, 0);   // transparent canvas
    for (int y = 0; y < dh; y++) {
        int sy = (int)((double)y * sh / dh);
        if (sy >= sh) sy = sh - 1;
        for (int x = 0; x < dw; x++) {
            int sx = (int)((double)x * sw / dw);
            if (sx >= sw) sx = sw - 1;
            const uint8_t* sp = &src.pixels[(size_t)(sy * sw + sx) * 4];
            uint8_t* dp = &out.pixels[(size_t)((oy + y) * n + (ox + x)) * 4];
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp[3] = sp[3];
        }
    }
    return out;
}

} // namespace pxb
