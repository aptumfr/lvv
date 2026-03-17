#pragma once

#include "protocol/commands.hpp"
#include <json.hpp>
#include <string>
#include <vector>
#include <optional>
#include <map>
#include <mutex>

namespace lvv {

/// Multi-property selector: key=value pairs that all must match a widget.
/// Supported keys: type, name, text, visible, clickable, auto_path
using WidgetSelector = std::map<std::string, std::string>;

/// Parse "type=button,text=OK,visible=true" into a WidgetSelector map.
WidgetSelector parse_selector(const std::string& expr);

/// Validate a parsed selector. Returns an error message, or empty string if valid.
/// Checks: non-empty, all keys are recognized.
std::string validate_selector(const WidgetSelector& sel);

class WidgetTree {
public:
    /// Parse tree from get_tree JSON response
    void update(const nlohmann::json& tree_json);

    /// Find widget by name or auto-path
    std::optional<WidgetInfo> find_by_name(const std::string& name) const;

    /// Find widget matching all selector properties (DFS, first match)
    std::optional<WidgetInfo> find_by_selector(const WidgetSelector& sel) const;

    /// Find all widgets matching selector
    std::vector<WidgetInfo> find_all_by_selector(const WidgetSelector& sel) const;

    /// Find widget at screen coordinates
    std::optional<WidgetInfo> find_at(int x, int y) const;

    /// Get all widgets flattened
    std::vector<const WidgetInfo*> flatten() const;

    /// Get the root
    const WidgetInfo& root() const { return root_; }

    /// Serialize to JSON
    nlohmann::json to_json() const;

    bool empty() const { return root_.children.empty() && root_.name.empty(); }

    /// Lock for thread-safe access from concurrent Crow handlers
    mutable std::mutex mutex;

private:
    WidgetInfo root_;

    static std::optional<WidgetInfo> find_recursive(
        const WidgetInfo& node, const std::string& name);
    static bool matches_selector(const WidgetInfo& node, const WidgetSelector& sel);
    static std::optional<WidgetInfo> find_selector_recursive(
        const WidgetInfo& node, const WidgetSelector& sel);
    static void find_all_selector_recursive(
        const WidgetInfo& node, const WidgetSelector& sel, std::vector<WidgetInfo>& out);
    static std::optional<WidgetInfo> find_at_recursive(
        const WidgetInfo& node, int x, int y);
    static void flatten_recursive(
        const WidgetInfo& node, std::vector<const WidgetInfo*>& out);
    static nlohmann::json widget_to_json(const WidgetInfo& w);
};

} // namespace lvv
