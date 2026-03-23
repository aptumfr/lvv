#include "tree_snapshot.hpp"

#include <map>

namespace lvv {

static nlohmann::json normalize_node(const WidgetInfo& w) {
    nlohmann::json j;
    j["type"] = w.type;
    j["name"] = w.name;
    j["text"] = w.text;
    j["visible"] = w.visible;
    j["clickable"] = w.clickable;

    if (!w.children.empty()) {
        j["children"] = nlohmann::json::array();
        for (const auto& child : w.children) {
            j["children"].push_back(normalize_node(child));
        }
    }
    return j;
}

nlohmann::json normalize_tree(const WidgetInfo& root) {
    return normalize_node(root);
}

// Build a display label for a node (name if set, else type)
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
                                     const std::string& path) {
    std::vector<std::string> diffs;
    auto label = path.empty() ? node_label(expected) : path;

    // Compare properties
    for (const auto& prop : {"type", "name", "text"}) {
        auto ev = expected.value(prop, "");
        auto av = actual.value(prop, "");
        if (ev != av) {
            diffs.push_back(label + ": property '" + prop + "' changed: '"
                          + ev + "' -> '" + av + "'");
        }
    }
    for (const auto& prop : {"visible", "clickable"}) {
        bool ev = expected.value(prop, false);
        bool av = actual.value(prop, false);
        if (ev != av) {
            diffs.push_back(label + ": property '" + prop + "' changed: "
                          + (ev ? "true" : "false") + " -> "
                          + (av ? "true" : "false"));
        }
    }

    // Compare children — match by name when possible, fall back to index for unnamed
    auto ec = expected.value("children", nlohmann::json::array());
    auto ac = actual.value("children", nlohmann::json::array());

    // Build name→index map for named children in actual.
    // Note: duplicate names are not supported — last one wins. LVGL widget names
    // should be unique among siblings; duplicates indicate a bug in the app.
    std::map<std::string, size_t> actual_by_name;
    for (size_t i = 0; i < ac.size(); i++) {
        auto name = ac[i].value("name", "");
        if (!name.empty()) actual_by_name[name] = i;
    }

    // Track which actual children have been matched
    std::vector<bool> actual_matched(ac.size(), false);

    // Match expected children: by name first, then by index
    for (size_t i = 0; i < ec.size(); i++) {
        auto expected_name = ec[i].value("name", "");
        auto child_label = join_path(label, node_label(ec[i]));

        if (!expected_name.empty()) {
            auto it = actual_by_name.find(expected_name);
            if (it != actual_by_name.end()) {
                // Matched by name
                actual_matched[it->second] = true;
                auto child_diffs = diff_trees(ec[i], ac[it->second], child_label);
                diffs.insert(diffs.end(), child_diffs.begin(), child_diffs.end());
                continue;
            }
        }

        // Fall back to index match for unnamed children
        if (i < ac.size() && !actual_matched[i]) {
            actual_matched[i] = true;
            auto child_diffs = diff_trees(ec[i], ac[i], child_label);
            diffs.insert(diffs.end(), child_diffs.begin(), child_diffs.end());
        } else {
            diffs.push_back(label + ": missing child '" + node_label(ec[i]) + "'");
        }
    }

    // Report unmatched actual children as extras
    for (size_t i = 0; i < ac.size(); i++) {
        if (!actual_matched[i]) {
            diffs.push_back(label + ": extra child '" + node_label(ac[i]) + "'");
        }
    }

    return diffs;
}

} // namespace lvv
