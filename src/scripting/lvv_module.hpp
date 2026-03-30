#pragma once

#include <string>
#include <json.hpp>

namespace lvv {

class Protocol;

/// Per-VM context for the lvv Python module.
/// Owned by ScriptEngine, accessed via py_getvmctx() on the VM thread.
/// Invariant: exactly one ScriptEngine (and therefore one LvvModuleContext)
/// exists per process, enforced by App owning the single instance. The design
/// supports multiple engines per-thread via py_getvmctx(), but LVV does not
/// create more than one.
struct LvvModuleContext {
    Protocol* protocol = nullptr;
    nlohmann::json object_map;
    std::string ref_images_dir = "ref_images";
    double default_threshold = 0.1;
    std::string captured_output;  // PocketPy print() output buffer
};

/// Register the 'lvv' Python module in PocketPy
void lvv_module_register();

/// Set the context for the current PocketPy VM (calls py_setvmctx)
void lvv_module_set_context(LvvModuleContext* ctx);

/// Reset per-run state (object map, etc.)
void lvv_module_reset_state();

} // namespace lvv
