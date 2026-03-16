#include <doctest/doctest.h>

#include "core/screen_capture.hpp"

using namespace lvv;

static Image make_test_image(int w, int h) {
    Image img;
    img.width = w;
    img.height = h;
    img.channels = 4;
    img.pixels.resize(w * h * 4);
    // Gradient pattern for non-trivial content
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            auto* p = img.pixels.data() + (y * w + x) * 4;
            p[0] = static_cast<uint8_t>(x % 256);
            p[1] = static_cast<uint8_t>(y % 256);
            p[2] = static_cast<uint8_t>((x + y) % 256);
            p[3] = 255;
        }
    }
    return img;
}

TEST_CASE("decode_raw_pixels ARGB8888") {
    // LVGL ARGB8888: memory layout is [B, G, R, A] (little-endian BGRA)
    int w = 2, h = 2;
    int stride = w * 4;
    std::vector<uint8_t> raw(stride * h);

    // Pixel (0,0): B=30, G=20, R=10, A=255
    raw[0] = 30; raw[1] = 20; raw[2] = 10; raw[3] = 255;
    // Pixel (1,0): B=60, G=50, R=40, A=128
    raw[4] = 60; raw[5] = 50; raw[6] = 40; raw[7] = 128;
    // Pixel (0,1): B=90, G=80, R=70, A=255
    raw[8] = 90; raw[9] = 80; raw[10] = 70; raw[11] = 255;
    // Pixel (1,1): B=120, G=110, R=100, A=200
    raw[12] = 120; raw[13] = 110; raw[14] = 100; raw[15] = 200;

    auto img = decode_raw_pixels(raw, w, h, stride,
                                  static_cast<int>(LvColorFormat::ARGB8888));

    REQUIRE(img.valid());
    CHECK(img.width == 2);
    CHECK(img.height == 2);
    CHECK(img.channels == 4);

    // Output is RGBA, check pixel (0,0)
    CHECK(img.pixels[0] == 10);   // R
    CHECK(img.pixels[1] == 20);   // G
    CHECK(img.pixels[2] == 30);   // B
    CHECK(img.pixels[3] == 255);  // A
}

TEST_CASE("decode_raw_pixels RGB888") {
    // LVGL RGB888: memory layout is [B, G, R] (little-endian BGR)
    int w = 2, h = 1;
    int stride = w * 3;
    std::vector<uint8_t> raw(stride * h);

    // Pixel (0,0): B=30, G=20, R=10
    raw[0] = 30; raw[1] = 20; raw[2] = 10;
    // Pixel (1,0): B=60, G=50, R=40
    raw[3] = 60; raw[4] = 50; raw[5] = 40;

    auto img = decode_raw_pixels(raw, w, h, stride,
                                  static_cast<int>(LvColorFormat::RGB888));

    REQUIRE(img.valid());
    // Output RGBA: R, G, B, A=255
    CHECK(img.pixels[0] == 10);
    CHECK(img.pixels[1] == 20);
    CHECK(img.pixels[2] == 30);
    CHECK(img.pixels[3] == 255);

    CHECK(img.pixels[4] == 40);
    CHECK(img.pixels[5] == 50);
    CHECK(img.pixels[6] == 60);
    CHECK(img.pixels[7] == 255);
}

TEST_CASE("decode_raw_pixels RGB565") {
    int w = 1, h = 1;
    int stride = w * 2;
    std::vector<uint8_t> raw(stride);

    // RGB565 little-endian: R=31 (5 bits), G=63 (6 bits), B=31 (5 bits) = white
    // 0xFFFF in LE = [0xFF, 0xFF]
    raw[0] = 0xFF;
    raw[1] = 0xFF;

    auto img = decode_raw_pixels(raw, w, h, stride,
                                  static_cast<int>(LvColorFormat::RGB565));

    REQUIRE(img.valid());
    // All channels should be close to 255
    CHECK(img.pixels[0] >= 248);  // R
    CHECK(img.pixels[1] >= 252);  // G
    CHECK(img.pixels[2] >= 248);  // B
    CHECK(img.pixels[3] == 255);  // A
}

TEST_CASE("encode_png produces valid data") {
    auto img = make_test_image(16, 16);
    auto data = encode_png(img);

    REQUIRE(data.size() > 8);
    // PNG magic number
    CHECK(data[0] == 0x89);
    CHECK(data[1] == 'P');
    CHECK(data[2] == 'N');
    CHECK(data[3] == 'G');
}

TEST_CASE("encode_jpeg produces valid data") {
    auto img = make_test_image(16, 16);
    auto data = encode_jpeg(img, 90);

    REQUIRE(data.size() > 2);
    // JPEG magic number (SOI marker)
    CHECK(data[0] == 0xFF);
    CHECK(data[1] == 0xD8);
}

TEST_CASE("encode_png and encode_jpeg produce non-empty output") {
    auto img = make_test_image(100, 100);

    auto png = encode_png(img);
    auto jpeg = encode_jpeg(img, 80);

    CHECK(png.size() > 0);
    CHECK(jpeg.size() > 0);
}

TEST_CASE("Image validity checks") {
    Image empty;
    CHECK_FALSE(empty.valid());

    Image no_pixels;
    no_pixels.width = 10;
    no_pixels.height = 10;
    CHECK_FALSE(no_pixels.valid());

    auto good = make_test_image(10, 10);
    CHECK(good.valid());
}
