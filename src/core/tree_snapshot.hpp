#pragma once

#include "protocol/commands.hpp"
#include <json.hpp>
#include <string>
#include <vector>

namespace lvv {

/// Normalize a widget tree for structural comparison.
/// Strips volatile fields (id, x, y, width, height, auto_path) and keeps
/// only structural properties (type, name, text, visible, clickable, children).
nlohmann::json normalize_tree(const WidgetInfo& root);

/// Compare two normalized trees and return a list of differences.
/// Each difference is a human-readable string like:
///   "home_screen/nav_row: missing child 'btn_extra'"
///   "settings_screen: property 'visible' changed: true -> false"
/// Returns an empty vector if trees are identical.
std::vector<std::string> diff_trees(const nlohmann::json& expected,
                                     const nlohmann::json& actual,
                                     const std::string& path = "");

} // namespace lvv
