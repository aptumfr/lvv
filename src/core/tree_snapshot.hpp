#pragma once

#include "protocol/commands.hpp"
#include <json.hpp>
#include <string>
#include <vector>

namespace lvv {

struct TreeSnapshotOptions {
    bool include_geometry = false;
    int geometry_tolerance = 0;  // absolute pixel tolerance for x/y/width/height
};

/// Normalize a widget tree for structural comparison.
/// Strips volatile fields (id, auto_path) and optionally includes geometry.
nlohmann::json normalize_tree(const WidgetInfo& root,
                               const TreeSnapshotOptions& opts = {});

/// Find a named widget in the tree. Returns nullptr if not found.
const WidgetInfo* find_subtree(const WidgetInfo& root, const std::string& name);

/// Compare two normalized trees and return a list of differences.
/// Returns an empty vector if trees are identical.
std::vector<std::string> diff_trees(const nlohmann::json& expected,
                                     const nlohmann::json& actual,
                                     const std::string& path = "",
                                     int geometry_tolerance = 0);

} // namespace lvv
