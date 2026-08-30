// pxb_thumbnailer.cpp
//
// Freedesktop thumbnailer for .pxb files.
// Reads the source path (%i), writes a scaled PNG to the output path (%o) at the
// requested size (%s). Reuses the shared pxb core (gzip + PNG decode) so the
// rendered thumbnail is pixel-identical to the preview app.
//
// Build:
//   c++ -std=c++17 -O2 pxb_thumbnailer.cpp \
//       ../../src/core/gzip.cpp ../../src/core/image.cpp \
//       ../../src/core/fileutil.cpp ../../src/core/pxb_reader.cpp \
//       <miniz.c> -I../../src/core -I<stb_dir> -I<miniz_dir> -o pxb-thumbnailer
//
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "pxb_reader.h"
#include "fileutil.h"   // pxb::read_file_bytes — single UTF-8-safe read path

static std::string StripFileURI(const std::string& s) {
    if (s.rfind("file://", 0) == 0) {
        std::string p = s.substr(7);
        // Handle URL-encoded characters minimally (spaces).
        std::string out;
        for (size_t i = 0; i < p.size(); i++) {
            if (p[i] == '%' && i + 2 < p.size()) {
                int v = 0;
                sscanf(p.substr(i + 1, 2).c_str(), "%2x", &v);
                out += (char)v; i += 2;
            } else out += p[i];
        }
        return out;
    }
    return s;
}

// Nearest-neighbour scale to fit within `size`, preserving aspect ratio.
// Returns the scaled RGBA buffer and reports the actual output dimensions via
// out_w / out_h. If the image is already small enough (or size is invalid),
// the original buffer is returned unchanged (out_w/out_h = img dimensions).
static std::vector<uint8_t> Scale(const pxb::RgbaImage& img, int size,
                                  int& out_w, int& out_h) {
    out_w = img.width;
    out_h = img.height;
    // Guard: never scale to a non-positive size, and skip if already small.
    if (size <= 0 || (img.width <= size && img.height <= size) ||
        img.width <= 0 || img.height <= 0) {
        return img.pixels;
    }

    float s = (float)size / (float)(img.width > img.height ? img.width : img.height);
    int nw = (int)(img.width * s + 0.5f);
    int nh = (int)(img.height * s + 0.5f);
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    out_w = nw;
    out_h = nh;

    // Guard against overflow in the allocation (nw/nh are small here, but be safe).
    if ((long long)nw * nh > (1LL << 28)) {  // ~268M px cap
        return img.pixels;
    }

    std::vector<uint8_t> out((size_t)nw * nh * 4);
    const int w = img.width, h = img.height;
    for (int y = 0; y < nh; y++) {
        int sy = (int)((y * (long long)h) / nh);   // nh > 0 guaranteed
        const uint8_t* src_row = &img.pixels[(size_t)sy * w * 4];
        uint8_t* dst_row = &out[(size_t)y * nw * 4];
        for (int x = 0; x < nw; x++) {
            int sx = (int)((x * (long long)w) / nw); // nw > 0 guaranteed
            memcpy(dst_row + x * 4, src_row + sx * 4, 4);
        }
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <input> <output> [size]\n", argv[0]); return 1; }
    std::string input = StripFileURI(argv[1]);
    std::string output = argv[2];
    int size = (argc >= 4) ? atoi(argv[3]) : 128;
    if (size <= 0) size = 128;   // guard: negative/zero requested size

    // Read the source file via the shared UTF-8-safe path (CreateFileW on
    // Windows, std::ifstream elsewhere) so behaviour matches the preview app
    // and macOS/Linux path-encoding differences stay centralised in fileutil.
    std::string err;
    std::vector<uint8_t> bytes = pxb::read_file_bytes(input, &err);
    if (bytes.empty()) {
        fprintf(stderr, "cannot open %s: %s\n", input.c_str(), err.c_str());
        return 1;
    }

    pxb::RgbaImage img = pxb::read_thumbnail_memory(bytes);
    if (img.empty()) { fprintf(stderr, "no preview in %s\n", input.c_str()); return 1; }

    int nw = 0, nh = 0;
    std::vector<uint8_t> out = Scale(img, size, nw, nh);

    if (!stbi_write_png(output.c_str(), nw, nh, 4, out.data(), nw * 4)) {
        fprintf(stderr, "failed to write %s\n", output.c_str());
        return 1;
    }
    return 0;
}
