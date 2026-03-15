#include "widget_tree.hpp"

namespace lvv {

static WidgetInfo parse_node(const nlohmann::json& j) {
    WidgetInfo w;
    w.id = j.value("id", 0);
    w.name = j.value("name", "");
    w.type = j.value("type", "");
    w.x = j.value("x", 0);
    w.y = j.value("y", 0);
    w.width = j.value("width", 0);
    w.height = j.value("height", 0);
    w.visible = j.value("visible", false);
    w.clickable = j.value("clickable", false);
    w.auto_path = j.value("auto_path", "");
    w.text = j.value("text", "");

    if (j.contains("children") && j["children"].is_array()) {
        for (const auto& child : j["children"]) {
            w.children.push_back(parse_node(child));
        }
    }
    return w;
}

void WidgetTree::update(const nlohmann::json& tree_json) {
    if (tree_json.contains("tree")) {
        root_ = parse_node(tree_json["tree"]);
    } else {
        root_ = parse_node(tree_json);
    }
}

std::optional<WidgetInfo> WidgetTree::find_by_name(const std::string& name) const {
    return find_recursive(root_, name);
}

std::optional<WidgetInfo> WidgetTree::find_at(int x, int y) const {
    return find_at_recursive(root_, x, y);
}

std::vector<const WidgetInfo*> WidgetTree::flatten() const {
    std::vector<const WidgetInfo*> out;
    flatten_recursive(root_, out);
    return out;
}

nlohmann::json WidgetTree::to_json() const {
    return widget_to_json(root_);
}

std::optional<WidgetInfo> WidgetTree::find_recursive(
    const WidgetInfo& node, const std::string& name) {
    if (node.name == name || node.auto_path == name) {
        return node;
    }
    for (const auto& child : node.children) {
        auto result = find_recursive(child, name);
        if (result) return result;
    }
    return std::nullopt;
}

std::optional<WidgetInfo> WidgetTree::find_at_recursive(
    const WidgetInfo& node, int x, int y) {
    // Deepest matching child wins
    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
        auto result = find_at_recursive(*it, x, y);
        if (result) return result;
    }
    if (x >= node.x && x < node.x + node.width &&
        y >= node.y && y < node.y + node.height &&
        node.visible) {
        return node;
    }
    return std::nullopt;
}

void WidgetTree::flatten_recursive(
    const WidgetInfo& node, std::vector<const WidgetInfo*>& out) {
    out.push_back(&node);
    for (const auto& child : node.children) {
        flatten_recursive(child, out);
    }
}

nlohmann::json WidgetTree::widget_to_json(const WidgetInfo& w) {
    nlohmann::json j;
    j["id"] = w.id;
    j["name"] = w.name;
    j["type"] = w.type;
    j["x"] = w.x;
    j["y"] = w.y;
    j["width"] = w.width;
    j["height"] = w.height;
    j["visible"] = w.visible;
    j["clickable"] = w.clickable;
    j["auto_path"] = w.auto_path;
    j["text"] = w.text;

    if (!w.children.empty()) {
        j["children"] = nlohmann::json::array();
        for (const auto& child : w.children) {
            j["children"].push_back(widget_to_json(child));
        }
    }
    return j;
}

} // namespace lvv
