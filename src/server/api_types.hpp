#pragma once

#include "protocol/commands.hpp"
#include <json.hpp>
#include <string>
#include <vector>

namespace lvv {

/// JSON-serializable widget response (used by multiple API routes)
struct WidgetJson {
    std::string name, type, auto_path, text;
    int x = 0, y = 0, width = 0, height = 0;
    bool visible = false, clickable = false;

    static WidgetJson from(const WidgetInfo& w) {
        return {w.name, w.type, w.auto_path, w.text,
                w.x, w.y, w.width, w.height,
                w.visible, w.clickable};
    }

    nlohmann::json to_json() const {
        return {{"name", name}, {"type", type},
                {"auto_path", auto_path}, {"text", text},
                {"x", x}, {"y", y},
                {"width", width}, {"height", height},
                {"visible", visible}, {"clickable", clickable}};
    }

    /// Best selector for this widget: name if set, else auto_path, else null
    nlohmann::json selector() const {
        if (!name.empty()) return name;
        if (!auto_path.empty()) return auto_path;
        return nullptr;
    }

    /// to_json with "found" and optional "selector" fields (for find-at, find-by, find)
    nlohmann::json to_find_json() const {
        auto j = to_json();
        j["found"] = true;
        j["selector"] = selector();
        return j;
    }
};

/// Serialize a list of widgets to a JSON array
inline nlohmann::json widgets_to_json(const std::vector<WidgetInfo>& widgets) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& w : widgets) {
        arr.push_back(WidgetJson::from(w).to_json());
    }
    return arr;
}

/// Serialize a list of widget pointers to a JSON array
inline nlohmann::json widgets_to_json(const std::vector<const WidgetInfo*>& widgets) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto* w : widgets) {
        arr.push_back(WidgetJson::from(*w).to_json());
    }
    return arr;
}

} // namespace lvv
