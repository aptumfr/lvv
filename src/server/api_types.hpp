#pragma once

#include "protocol/commands.hpp"
#include "core/visual_regression.hpp"
#include <json.hpp>
#include <optional>
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

// ---- Request parsing helpers ----

/// Parse {x, y} from JSON body. Returns nullopt if missing or wrong type.
struct CoordsRequest {
    int x = 0, y = 0;

    static std::optional<CoordsRequest> parse(const nlohmann::json& body) {
        try {
            if (!body.contains("x") || !body.contains("y")) return std::nullopt;
            return CoordsRequest{body["x"].get<int>(), body["y"].get<int>()};
        } catch (...) { return std::nullopt; }
    }
};

/// Parse {name} from JSON body.
struct NameRequest {
    std::string name;

    static std::optional<NameRequest> parse(const nlohmann::json& body) {
        try {
            if (!body.contains("name")) return std::nullopt;
            return NameRequest{body["name"].get<std::string>()};
        } catch (...) { return std::nullopt; }
    }
};

/// Parse {text} from JSON body.
struct TextRequest {
    std::string text;

    static std::optional<TextRequest> parse(const nlohmann::json& body) {
        try {
            if (!body.contains("text")) return std::nullopt;
            return TextRequest{body["text"].get<std::string>()};
        } catch (...) { return std::nullopt; }
    }
};

/// Parse {key} from JSON body.
struct KeyRequest {
    std::string key;

    static std::optional<KeyRequest> parse(const nlohmann::json& body) {
        try {
            if (!body.contains("key")) return std::nullopt;
            return KeyRequest{body["key"].get<std::string>()};
        } catch (...) { return std::nullopt; }
    }
};

/// Parse {x, y, x_end, y_end, duration, steps} from JSON body.
struct GestureRequest {
    int x = 0, y = 0, x_end = 0, y_end = 0;
    int duration = 300, steps = 10;

    static std::optional<GestureRequest> parse(const nlohmann::json& body) {
        try {
            return GestureRequest{
                body.value("x", 0), body.value("y", 0),
                body.value("x_end", 0), body.value("y_end", 0),
                body.value("duration", 300), body.value("steps", 10)};
        } catch (...) { return std::nullopt; }
    }
};

/// Parse {reference, threshold, color_threshold, ignore_regions} from JSON body.
struct CompareRequest {
    std::string reference;
    double threshold = 0.1;
    double color_threshold = 10.0;
    std::vector<IgnoreRegion> ignore_regions;

    static std::optional<CompareRequest> parse(const nlohmann::json& body) {
        try {
            if (!body.contains("reference")) return std::nullopt;
            CompareRequest req;
            req.reference = body["reference"].get<std::string>();
            req.threshold = body.value("threshold", 0.1);
            req.color_threshold = body.value("color_threshold", 10.0);
            if (body.contains("ignore_regions")) {
                for (const auto& r : body["ignore_regions"]) {
                    req.ignore_regions.push_back({
                        r.value("x", 0), r.value("y", 0),
                        r.value("width", 0), r.value("height", 0)});
                }
            }
            return req;
        } catch (...) { return std::nullopt; }
    }
};

} // namespace lvv
