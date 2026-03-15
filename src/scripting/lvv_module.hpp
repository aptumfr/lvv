#pragma once

#include <string>

namespace lvv {

class Protocol;

/// Register the 'lvv' Python module in PocketPy
void lvv_module_register();

/// Set the active protocol instance for the module to use
void lvv_module_set_protocol(Protocol* protocol);

/// Reset per-run state (object map, etc.)
void lvv_module_reset_state();

/// Set default reference images directory and diff threshold
void lvv_module_set_defaults(const std::string& ref_dir, double threshold);

} // namespace lvv
