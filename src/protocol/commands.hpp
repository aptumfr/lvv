#pragma once

#include <string>
#include <vector>
#include <optional>

namespace lvv {

struct WidgetInfo {
    int id = 0;
    std::string name;
    std::string type;
    int x = 0, y = 0, width = 0, height = 0;
    bool visible = false;
    bool clickable = false;
    std::string auto_path;
    std::string text;
    std::vector<WidgetInfo> children;
};

struct ScreenInfo {
    int width = 0;
    int height = 0;
    std::string color_format;
};

struct PropResult {
    std::string name;
    std::string value;
};

} // namespace lvv
