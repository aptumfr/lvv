#include "tree_snapshot.hpp"

#include <cmath>
#include <map>

namespace lvv {

static nlohmann::json normalize_node(const WidgetInfo& w, const TreeSnapshotOptions& opts) {
    nlohmann::json j;
    j["type"] = w.type;
    j["name"] = w.name;
    j["text"] = w.text;
    j["visible"] = w.visible;
    j["clickable"] = w.clickable;

    if (opts.include_geometry) {
        j["x"] = w.x;
        j["y"] = w.y;
        j["width"] = w.width;
        j["height"] = w.height;
    }

    if (!w.children.empty()) {
        j["children"] = nlohmann::json::array();
        for (const auto& child : w.children) {
            j["children"].push_back(normalize_node(child, opts));
        }
    }
    return j;
}

nlohmann::json normalize_tree(const WidgetInfo& root, const TreeSnapshotOptions& opts) {
    return normalize_node(root, opts);
}

const WidgetInfo* find_subtree(const WidgetInfo& root, const std::string& name) {
    if (root.name == name) return &root;
    for (const auto& child : root.children) {
        const auto* found = find_subtree(child, name);
        if (found) return found;
    }
    return nullptr;
}

// Build a display label for a node
static std::string node_label(const nlohmann::json& node) {
    auto name = node.value("name", "");
    if (!name.empty()) return name;
    return node.value("type", "?");
}

static std::string join_path(const std::string& parent, const std::string& child) {
    if (parent.empty()) return child;
    return parent + "/" + child;
}

std::vector<std::string> diff_trees(const nlohmann::json& expected,
                                     const nlohmann::json& actual,
                                     const std::string& path,
                                     int geometry_tolerance) {
    std::vector<std::string> diffs;
    const auto label = path.empty() ? node_label(expected) : path;

    // Compare string properties
    for (const auto& prop : {"type", "name", "text"}) {
        const auto ev = expected.value(prop, "");
        const auto av = actual.value(prop, "");
        if (ev != av) {
            diffs.push_back(label + ": property '" + prop + "' changed: '"
                          + ev + "' -> '" + av + "'");
        }
    }

    // Compare boolean properties
    for (const auto& prop : {"visible", "clickable"}) {
        const bool ev = expected.value(prop, false);
        const bool av = actual.value(prop, false);
        if (ev != av) {
            diffs.push_back(label + ": property '" + prop + "' changed: "
                          + (ev ? "true" : "false") + " -> "
                          + (av ? "true" : "false"));
        }
    }

    // Compare geometry (only if present in expected — i.e. include_geometry was on)
    for (const auto& prop : {"x", "y", "width", "height"}) {
        if (!expected.contains(prop)) continue;
        const int ev = expected.value(prop, 0);
        const int av = actual.value(prop, 0);
        if (std::abs(ev - av) > geometry_tolerance) {
            diffs.push_back(label + ": geometry '" + prop + "' changed: "
                          + std::to_string(ev) + " -> " + std::to_string(av)
                          + (geometry_tolerance > 0
                             ? " (tolerance: " + std::to_string(geometry_tolerance) + ")"
                             : ""));
        }
    }

    // Compare children — match by name when possible, fall back to index for unnamed
    const auto ec = expected.value("children", nlohmann::json::array());
    const auto ac = actual.value("children", nlohmann::json::array());

    // Build name→index map for named children in actual.
    // Note: duplicate names are not supported — last one wins. LVGL widget names
    // should be unique among siblings; duplicates indicate a bug in the app.
    std::map<std::string, size_t> actual_by_name;
    for (size_t i = 0; i < ac.size(); i++) {
        const auto name = ac[i].value("name", "");
        if (!name.empty()) actual_by_name[name] = i;
    }

    std::vector<bool> actual_matched(ac.size(), false);

    for (size_t i = 0; i < ec.size(); i++) {
        const auto expected_name = ec[i].value("name", "");
        const auto child_label = join_path(label, node_label(ec[i]));

        if (!expected_name.empty()) {
            auto it = actual_by_name.find(expected_name);
            if (it != actual_by_name.end()) {
                actual_matched[it->second] = true;
                auto child_diffs = diff_trees(ec[i], ac[it->second],
                                               child_label, geometry_tolerance);
                diffs.insert(diffs.end(), child_diffs.begin(), child_diffs.end());
                continue;
            }
        }

        if (i < ac.size() && !actual_matched[i]) {
            actual_matched[i] = true;
            auto child_diffs = diff_trees(ec[i], ac[i], child_label, geometry_tolerance);
            diffs.insert(diffs.end(), child_diffs.begin(), child_diffs.end());
        } else {
            diffs.push_back(label + ": missing child '" + node_label(ec[i]) + "'");
        }
    }

    for (size_t i = 0; i < ac.size(); i++) {
        if (!actual_matched[i]) {
            diffs.push_back(label + ": extra child '" + node_label(ac[i]) + "'");
        }
    }

    return diffs;
}

} // namespace lvv
