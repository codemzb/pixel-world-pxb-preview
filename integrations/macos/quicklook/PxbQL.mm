// PxbQL.mm
//
// macOS Quick Look generator for .pxb files.
// Compiled as Objective-C++; it links the shared pxb parsing core (gzip + PNG
// via miniz/stb) and renders the embedded thumbnail to a CGImage, which is then
// drawn into the Quick Look context. One source of truth for the format.
//
#import <QuickLook/QuickLook.h>
#import <Cocoa/Cocoa.h>

#include <vector>
#include <fstream>
#include "pxb_reader.h"

static std::vector<uint8_t> ReadFile(CFURLRef url) {
    std::vector<uint8_t> out;
    if (!url) return out;
    char path[4096];
    if (!CFURLGetFileSystemRepresentation(url, true, (UInt8*)path, sizeof(path)))
        return out;
    std::ifstream f(path, std::ios::binary);
    if (!f) return out;
    f.seekg(0, std::ios::end);
    std::streamoff sz = f.tellg();
    f.seekg(0, std::ios::beg);
    if (sz <= 0) return out;
    out.resize((size_t)sz);
    f.read((char*)out.data(), sz);
    return out;
}

static CGImageRef CreatePxbCGImage(CFURLRef url) {
    std::vector<uint8_t> bytes = ReadFile(url);
    if (bytes.empty()) return nullptr;
    pxb::RgbaImage img = pxb::read_thumbnail_memory(bytes);
    if (img.empty()) return nullptr;

    int w = img.width, h = img.height;
    std::vector<uint8_t> buf((size_t)w * h * 4);
    // Premultiply RGBA for correct compositing in a CG context.
    for (int i = 0; i < w * h; i++) {
        const uint8_t* p = &img.pixels[(size_t)i * 4];
        uint8_t a = p[3];
        buf[(size_t)i * 4 + 0] = (uint8_t)((p[0] * a) / 255);
        buf[(size_t)i * 4 + 1] = (uint8_t)((p[1] * a) / 255);
        buf[(size_t)i * 4 + 2] = (uint8_t)((p[2] * a) / 255);
        buf[(size_t)i * 4 + 3] = a;
    }

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        buf.data(), w, h, 8, (size_t)w * 4, cs,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGImageRef image = nullptr;
    if (ctx) {
        image = CGBitmapContextCreateImage(ctx);
        CGContextRelease(ctx);
    }
    CGColorSpaceRelease(cs);
    // buf is copied by CGImage, safe to free after.
    return image;
}

extern "C" {

OSStatus GenerateThumbnailForURL(void* thisInterface,
                                QLThumbnailRequestRef thumbnail,
                                CFURLRef url,
                                CFStringRef contentTypeUTI,
                                CFDictionaryRef options,
                                CGSize maxSize) {
    CGImageRef image = CreatePxbCGImage(url);
    if (!image) return noErr;

    CGContextRef ctx = QLThumbnailRequestCreateContext(thumbnail, maxSize, true, NULL);
    if (ctx) {
        CGRect rect = CGRectMake(0, 0, maxSize.width, maxSize.height);
        // Scale preserving aspect ratio.
        float scale = MIN(maxSize.width / (float)CGImageGetWidth(image),
                          maxSize.height / (float)CGImageGetHeight(image));
        CGSize ds = { CGImageGetWidth(image) * scale, CGImageGetHeight(image) * scale };
        CGRect drawRect = CGRectMake((maxSize.width - ds.width) / 2,
                                     (maxSize.height - ds.height) / 2,
                                     ds.width, ds.height);
        // Flip Y so the image is upright.
        CGContextTranslateCTM(ctx, 0, maxSize.height);
        CGContextScaleCTM(ctx, 1.0, -1.0);
        CGContextDrawImage(ctx, drawRect, image);
        QLThumbnailRequestFlushContext(thumbnail, ctx);
        CFRelease(ctx);
    }
    CGImageRelease(image);
    return noErr;
}

void CancelThumbnailGeneration(void* thisInterface, QLThumbnailRequestRef thumbnail) {
    // Nothing to cancel in this synchronous generator.
}

} // extern "C"
