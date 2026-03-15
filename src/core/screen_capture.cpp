#include "screen_capture.hpp"

#include "stb_image.h"
#include "stb_image_write.h"

#include <cstring>

namespace lvv {

Image decode_raw_pixels(const std::vector<uint8_t>& raw, int width, int height,
                        int stride, int format) {
    // Validate dimensions to prevent out-of-bounds reads
    if (width <= 0 || height <= 0 || stride <= 0) return {};
    if (width > 16384 || height > 16384) return {};

    auto fmt = static_cast<LvColorFormat>(format);
    int bpp;
    switch (fmt) {
    case LvColorFormat::RGB565:   bpp = 2; break;
    case LvColorFormat::RGB888:   bpp = 3; break;
    case LvColorFormat::ARGB8888:
    case LvColorFormat::XRGB8888: bpp = 4; break;
    default:                      bpp = 4; break;
    }

    if (stride < width * bpp) return {};

    size_t required = static_cast<size_t>(height) * stride;
    if (raw.size() < required) return {};

    Image img;
    img.width = width;
    img.height = height;
    img.channels = 4;
    img.pixels.resize(static_cast<size_t>(width) * height * 4);

    for (int y = 0; y < height; y++) {
        const uint8_t* src = raw.data() + y * stride;
        uint8_t* dst = img.pixels.data() + y * width * 4;

        for (int x = 0; x < width; x++) {
            switch (fmt) {
            case LvColorFormat::ARGB8888:
                // LVGL ARGB8888: memory layout is B, G, R, A (little-endian)
                dst[0] = src[2]; // R
                dst[1] = src[1]; // G
                dst[2] = src[0]; // B
                dst[3] = src[3]; // A
                src += 4;
                break;

            case LvColorFormat::XRGB8888:
                dst[0] = src[2]; // R
                dst[1] = src[1]; // G
                dst[2] = src[0]; // B
                dst[3] = 255;    // opaque
                src += 4;
                break;

            case LvColorFormat::RGB888:
                dst[0] = src[2]; // R
                dst[1] = src[1]; // G
                dst[2] = src[0]; // B
                dst[3] = 255;
                src += 3;
                break;

            case LvColorFormat::RGB565: {
                uint16_t px = src[0] | (src[1] << 8);
                dst[0] = ((px >> 11) & 0x1F) << 3; // R
                dst[1] = ((px >> 5)  & 0x3F) << 2; // G
                dst[2] = (px         & 0x1F) << 3;  // B
                dst[3] = 255;
                src += 2;
                break;
            }

            default:
                // Unknown format — try as ARGB8888
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = src[3];
                src += 4;
                break;
            }

            dst += 4;
        }
    }

    return img;
}

// Callback for stbi_write_png_to_func
static void write_png_callback(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<uint8_t>*>(context);
    auto* bytes = static_cast<uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

std::vector<uint8_t> encode_png(const Image& img) {
    std::vector<uint8_t> out;
    stbi_write_png_to_func(
        write_png_callback, &out,
        img.width, img.height, img.channels,
        img.pixels.data(), img.width * img.channels);
    return out;
}

bool save_png(const Image& img, const std::string& path) {
    return stbi_write_png(
        path.c_str(), img.width, img.height, img.channels,
        img.pixels.data(), img.width * img.channels) != 0;
}

Image load_png(const std::string& path) {
    Image img;
    int channels;
    auto* data = stbi_load(path.c_str(), &img.width, &img.height, &channels, 4);
    if (!data) return {};

    img.channels = 4;
    img.pixels.assign(data, data + img.width * img.height * 4);
    stbi_image_free(data);
    return img;
}

} // namespace lvv
