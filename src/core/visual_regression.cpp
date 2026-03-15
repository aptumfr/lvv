#include "visual_regression.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace lvv {

static bool in_ignore_region(int px, int py, const std::vector<IgnoreRegion>& regions) {
    for (const auto& r : regions) {
        if (px >= r.x && px < r.x + r.width &&
            py >= r.y && py < r.y + r.height) {
            return true;
        }
    }
    return false;
}

static bool is_antialiased(const Image& img, int x, int y, double threshold) {
    // Check if this pixel is on an edge (adjacent to a color boundary)
    const int w = img.width;
    const int h = img.height;

    int high_contrast_neighbors = 0;
    const uint8_t* center = img.pixels.data() + (y * w + x) * 4;

    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;

            const uint8_t* neighbor = img.pixels.data() + (ny * w + nx) * 4;
            double max_diff = 0;
            for (int c = 0; c < 3; c++) {
                max_diff = std::max(max_diff,
                    std::abs(static_cast<double>(center[c]) - neighbor[c]));
            }
            if (max_diff > threshold) {
                high_contrast_neighbors++;
            }
        }
    }
    // If surrounded by high-contrast neighbors, likely anti-aliased
    return high_contrast_neighbors >= 3;
}

DiffResult compare_images(const Image& reference, const Image& actual,
                          const CompareOptions& opts) {
    DiffResult result;

    if (!reference.valid() || !actual.valid()) {
        return result;
    }

    if (reference.width != actual.width || reference.height != actual.height) {
        result.total_pixels = std::max(
            reference.width * reference.height,
            actual.width * actual.height);
        result.diff_pixels = result.total_pixels;
        result.diff_percentage = 100.0;
        return result;
    }

    const int w = reference.width;
    const int h = reference.height;
    result.total_pixels = w * h;

    // Create diff image (transparent green = same, red = different)
    result.diff_image.width = w;
    result.diff_image.height = h;
    result.diff_image.channels = 4;
    result.diff_image.pixels.resize(w * h * 4);

    int diff_count = 0;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (in_ignore_region(x, y, opts.ignore_regions)) {
                // Gray for ignored
                auto* dp = result.diff_image.pixels.data() + (y * w + x) * 4;
                dp[0] = 128; dp[1] = 128; dp[2] = 128; dp[3] = 255;
                continue;
            }

            const auto* rp = reference.pixels.data() + (y * w + x) * 4;
            const auto* ap = actual.pixels.data() + (y * w + x) * 4;
            auto* dp = result.diff_image.pixels.data() + (y * w + x) * 4;

            double max_diff = 0;
            for (int c = 0; c < 3; c++) {
                max_diff = std::max(max_diff,
                    std::abs(static_cast<double>(rp[c]) - ap[c]));
            }

            bool is_diff = max_diff > opts.color_threshold;

            // Anti-aliasing check: skip if pixel is on an edge in either image
            if (is_diff && opts.anti_aliasing) {
                if (is_antialiased(reference, x, y, opts.color_threshold) ||
                    is_antialiased(actual, x, y, opts.color_threshold)) {
                    is_diff = false;
                }
            }

            if (is_diff) {
                diff_count++;
                // Red highlight
                dp[0] = 255; dp[1] = 0; dp[2] = 0; dp[3] = 255;
            } else {
                // Dimmed copy of actual
                dp[0] = ap[0] / 3; dp[1] = ap[1] / 3; dp[2] = ap[2] / 3; dp[3] = 255;
            }
        }
    }

    result.diff_pixels = diff_count;
    result.diff_percentage = (result.total_pixels > 0)
        ? (100.0 * diff_count / result.total_pixels)
        : 0.0;
    result.identical = (diff_count == 0);
    result.passed = (result.diff_percentage <= opts.diff_threshold);

    return result;
}

} // namespace lvv
