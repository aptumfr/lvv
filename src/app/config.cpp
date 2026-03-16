#include "config.hpp"

#include <filesystem>

#ifdef __linux__
#include <unistd.h>
#include <climits>
#endif

namespace lvv {

static std::string exe_dir() {
#ifdef __linux__
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return std::filesystem::path(buf).parent_path().string();
    }
#endif
    // Fallback: current working directory
    return std::filesystem::current_path().string();
}

std::string default_static_dir() {
    // Look for web/dist relative to the executable's directory
    // (works for both in-tree builds and installed layouts)
    auto dir = exe_dir();

    // In-tree build: <project>/build/lvv -> <project>/web/dist
    auto in_tree = std::filesystem::path(dir).parent_path() / "web" / "dist";
    if (std::filesystem::is_directory(in_tree)) {
        return in_tree.string();
    }

    // Installed layout: <prefix>/bin/lvv -> <prefix>/share/lvv/web
    auto installed = std::filesystem::path(dir).parent_path() / "share" / "lvv" / "web";
    if (std::filesystem::is_directory(installed)) {
        return installed.string();
    }

    // Last resort: relative to cwd
    return "web/dist";
}

} // namespace lvv
