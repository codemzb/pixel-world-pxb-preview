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

} // namespace pxb
