#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lvv {

struct Image {
    int width = 0;
    int height = 0;
    int channels = 4; // RGBA
    std::vector<uint8_t> pixels;

    bool valid() const { return width > 0 && height > 0 && !pixels.empty(); }
};

/// LVGL color format codes (from lv_color.h)
enum class LvColorFormat : int {
    RGB565   = 7,
    RGB888   = 15,
    ARGB8888 = 16,
    XRGB8888 = 17,
};

/// Decode raw LVGL pixel data into an RGBA Image
Image decode_raw_pixels(const std::vector<uint8_t>& raw, int width, int height,
                        int stride, int format);

/// Encode an Image to PNG data
std::vector<uint8_t> encode_png(const Image& img);

/// Save an Image to a PNG file
bool save_png(const Image& img, const std::string& path);

/// Load an Image from a PNG file
Image load_png(const std::string& path);

} // namespace lvv
