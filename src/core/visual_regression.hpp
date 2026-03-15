#pragma once

#include "screen_capture.hpp"
#include <vector>

namespace lvv {

struct IgnoreRegion {
    int x, y, width, height;
};

struct CompareOptions {
    double color_threshold = 10.0;    // Per-channel difference (0-255)
    double diff_threshold = 0.1;      // Max allowed diff percentage
    std::vector<IgnoreRegion> ignore_regions;
    bool anti_aliasing = true;
};

struct DiffResult {
    bool identical = false;
    bool passed = false;
    double diff_percentage = 0.0;
    int diff_pixels = 0;
    int total_pixels = 0;
    Image diff_image;
};

/// Compare two images for visual regression
DiffResult compare_images(const Image& reference, const Image& actual,
                          const CompareOptions& opts = {});

} // namespace lvv
