#include <doctest/doctest.h>

#include "core/visual_regression.hpp"

using namespace lvv;

static Image make_solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    Image img;
    img.width = w;
    img.height = h;
    img.channels = 4;
    img.pixels.resize(w * h * 4);
    for (int i = 0; i < w * h; i++) {
        img.pixels[i * 4 + 0] = r;
        img.pixels[i * 4 + 1] = g;
        img.pixels[i * 4 + 2] = b;
        img.pixels[i * 4 + 3] = a;
    }
    return img;
}

TEST_CASE("identical images pass") {
    auto img = make_solid(100, 100, 128, 64, 32);
    auto result = compare_images(img, img);

    CHECK(result.identical);
    CHECK(result.passed);
    CHECK(result.diff_percentage == 0.0);
    CHECK(result.diff_pixels == 0);
    CHECK(result.total_pixels == 10000);
}

TEST_CASE("completely different images fail") {
    auto ref = make_solid(100, 100, 0, 0, 0);
    auto actual = make_solid(100, 100, 255, 255, 255);

    CompareOptions opts;
    opts.anti_aliasing = false;
    opts.diff_threshold = 0.1;
    auto result = compare_images(ref, actual, opts);

    CHECK_FALSE(result.identical);
    CHECK_FALSE(result.passed);
    CHECK(result.diff_percentage == 100.0);
    CHECK(result.diff_pixels == 10000);
}

TEST_CASE("small difference within threshold passes") {
    auto ref = make_solid(100, 100, 100, 100, 100);
    auto actual = make_solid(100, 100, 105, 100, 100);

    CompareOptions opts;
    opts.color_threshold = 10.0;
    opts.diff_threshold = 0.1;
    auto result = compare_images(ref, actual, opts);

    CHECK(result.identical);
    CHECK(result.passed);
}

TEST_CASE("small difference exceeding color_threshold") {
    auto ref = make_solid(100, 100, 100, 100, 100);
    auto actual = make_solid(100, 100, 115, 100, 100);

    CompareOptions opts;
    opts.color_threshold = 10.0;
    opts.diff_threshold = 50.0;  // high to allow pass, we're testing pixel detection
    opts.anti_aliasing = false;
    auto result = compare_images(ref, actual, opts);

    CHECK_FALSE(result.identical);
    CHECK(result.diff_pixels == 10000);
}

TEST_CASE("size mismatch returns 100% diff") {
    auto ref = make_solid(100, 100, 0, 0, 0);
    auto actual = make_solid(200, 200, 0, 0, 0);

    auto result = compare_images(ref, actual);

    CHECK_FALSE(result.passed);
    CHECK(result.diff_percentage == 100.0);
}

TEST_CASE("invalid images return empty result") {
    Image invalid;
    auto good = make_solid(10, 10, 0, 0, 0);

    auto r1 = compare_images(invalid, good);
    CHECK(r1.total_pixels == 0);
    CHECK_FALSE(r1.passed);

    auto r2 = compare_images(good, invalid);
    CHECK(r2.total_pixels == 0);
}

TEST_CASE("ignore regions are excluded from comparison") {
    auto ref = make_solid(100, 100, 0, 0, 0);
    auto actual = make_solid(100, 100, 255, 255, 255);

    CompareOptions opts;
    opts.anti_aliasing = false;
    opts.diff_threshold = 0.1;
    // Ignore the entire image
    opts.ignore_regions = {{0, 0, 100, 100}};

    auto result = compare_images(ref, actual, opts);

    CHECK(result.diff_pixels == 0);
    CHECK(result.total_pixels == 0);
    CHECK(result.passed);
}

TEST_CASE("partial ignore region reduces total pixels") {
    auto ref = make_solid(100, 100, 0, 0, 0);
    auto actual = make_solid(100, 100, 255, 255, 255);

    CompareOptions opts;
    opts.anti_aliasing = false;
    opts.diff_threshold = 0.1;
    // Ignore top-left 50x50
    opts.ignore_regions = {{0, 0, 50, 50}};

    auto result = compare_images(ref, actual, opts);

    CHECK(result.total_pixels == 10000 - 2500);
    CHECK(result.diff_pixels == 7500);
}

TEST_CASE("diff_image has correct dimensions") {
    auto ref = make_solid(50, 30, 0, 0, 0);
    auto actual = make_solid(50, 30, 255, 0, 0);

    auto result = compare_images(ref, actual);

    CHECK(result.diff_image.width == 50);
    CHECK(result.diff_image.height == 30);
    CHECK(result.diff_image.valid());
}
