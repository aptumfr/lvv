#pragma once

#include "protocol/commands.hpp"
#include <json.hpp>
#include <string>
#include <vector>
#include <optional>

namespace lvv {

class WidgetTree {
public:
    /// Parse tree from get_tree JSON response
    void update(const nlohmann::json& tree_json);

    /// Find widget by name or auto-path
    std::optional<WidgetInfo> find_by_name(const std::string& name) const;

    /// Find widget at screen coordinates
    std::optional<WidgetInfo> find_at(int x, int y) const;

    /// Get all widgets flattened
    std::vector<const WidgetInfo*> flatten() const;

    /// Get the root
    const WidgetInfo& root() const { return root_; }

    /// Serialize to JSON
    nlohmann::json to_json() const;

    bool empty() const { return root_.children.empty() && root_.name.empty(); }

private:
    WidgetInfo root_;

    static std::optional<WidgetInfo> find_recursive(
        const WidgetInfo& node, const std::string& name);
    static std::optional<WidgetInfo> find_at_recursive(
        const WidgetInfo& node, int x, int y);
    static void flatten_recursive(
        const WidgetInfo& node, std::vector<const WidgetInfo*>& out);
    static nlohmann::json widget_to_json(const WidgetInfo& w);
};

} // namespace lvv
