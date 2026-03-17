#include "widget_tree.hpp"

#include <set>
#include <sstream>

namespace lvv {

static const std::set<std::string> valid_selector_keys = {
    "type", "name", "text", "visible", "clickable", "auto_path"
};

static const std::set<std::string> valid_bool_values = {
    "true", "false", "1", "0"
};

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(' ');
    if (start == std::string::npos) return {};
    size_t end = s.find_last_not_of(' ');
    return s.substr(start, end - start + 1);
}

// Find positions where a ",key=" boundary starts (comma followed by a known key and '=').
// This allows values to contain commas and '=' characters.
static std::vector<size_t> find_pair_boundaries(const std::string& expr) {
    std::vector<size_t> boundaries;
    boundaries.push_back(0);
    for (size_t i = 0; i < expr.size(); ++i) {
        if (expr[i] != ',') continue;
        // Check if what follows the comma is "key=" for a known key
        auto rest = expr.substr(i + 1);
        // Trim leading spaces
        size_t skip = rest.find_first_not_of(' ');
        if (skip == std::string::npos) continue;
        rest = rest.substr(skip);
        for (const auto& key : valid_selector_keys) {
            if (rest.size() > key.size() && rest.compare(0, key.size(), key) == 0) {
                // Check that the key is followed by optional spaces then '='
                size_t k = key.size();
                while (k < rest.size() && rest[k] == ' ') ++k;
                if (k < rest.size() && rest[k] == '=') {
                    boundaries.push_back(i + 1);  // Start of next pair (after comma)
                    break;
                }
            }
        }
    }
    return boundaries;
}

WidgetSelector parse_selector(const std::string& expr) {
    WidgetSelector sel;
    if (expr.empty()) return sel;

    auto boundaries = find_pair_boundaries(expr);
    for (size_t i = 0; i < boundaries.size(); ++i) {
        size_t start = boundaries[i];
        size_t end = (i + 1 < boundaries.size())
            ? boundaries[i + 1] - 1  // -1 to exclude the comma
            : expr.size();
        auto token = expr.substr(start, end - start);
        auto eq = token.find('=');
        if (eq == std::string::npos) continue;
        auto key = trim(token.substr(0, eq));
        auto val = trim(token.substr(eq + 1));
        if (!key.empty()) sel[key] = val;
    }
    return sel;
}

std::string validate_selector(const WidgetSelector& sel) {
    if (sel.empty()) return "empty selector";
    for (const auto& [key, val] : sel) {
        if (valid_selector_keys.find(key) == valid_selector_keys.end()) {
            return "unknown selector key: '" + key + "'";
        }
        if (key == "visible" || key == "clickable") {
            if (valid_bool_values.find(val) == valid_bool_values.end()) {
                return "invalid boolean value for '" + key + "': '" + val
                     + "' (expected true, false, 1, or 0)";
            }
        }
    }
    return {};
}

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

std::optional<WidgetInfo> WidgetTree::find_by_selector(const WidgetSelector& sel) const {
    return find_selector_recursive(root_, sel);
}

std::vector<WidgetInfo> WidgetTree::find_all_by_selector(const WidgetSelector& sel) const {
    std::vector<WidgetInfo> out;
    find_all_selector_recursive(root_, sel, out);
    return out;
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

bool WidgetTree::matches_selector(const WidgetInfo& node, const WidgetSelector& sel) {
    for (const auto& [key, val] : sel) {
        if (key == "type") {
            if (node.type != val) return false;
        } else if (key == "name") {
            if (node.name != val) return false;
        } else if (key == "text") {
            if (node.text != val) return false;
        } else if (key == "visible") {
            bool expected = (val == "true" || val == "1");
            if (node.visible != expected) return false;
        } else if (key == "clickable") {
            bool expected = (val == "true" || val == "1");
            if (node.clickable != expected) return false;
        } else if (key == "auto_path") {
            if (node.auto_path != val) return false;
        } else {
            return false;  // Unknown key never matches
        }
    }
    return true;
}

std::optional<WidgetInfo> WidgetTree::find_selector_recursive(
    const WidgetInfo& node, const WidgetSelector& sel) {
    if (matches_selector(node, sel)) return node;
    for (const auto& child : node.children) {
        auto result = find_selector_recursive(child, sel);
        if (result) return result;
    }
    return std::nullopt;
}

void WidgetTree::find_all_selector_recursive(
    const WidgetInfo& node, const WidgetSelector& sel, std::vector<WidgetInfo>& out) {
    if (matches_selector(node, sel)) out.push_back(node);
    for (const auto& child : node.children) {
        find_all_selector_recursive(child, sel, out);
    }
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
