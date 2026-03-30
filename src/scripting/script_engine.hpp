#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "lvv_module.hpp"

namespace lvv {

/// ScriptEngine runs PocketPy on a dedicated thread because
/// pk_current_vm is _Thread_local — all VM calls must happen
/// on the thread that called py_initialize().
class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();

    // Non-copyable
    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    /// Set the active protocol for Python bindings to use
    void set_protocol(Protocol* protocol);

    /// Set visual regression defaults (ref images dir, diff threshold)
    void set_visual_defaults(const std::string& ref_dir, double threshold);

    /// Run a Python file, returns (success, output/error)
    std::pair<bool, std::string> run_file(const std::string& path);

    /// Run a Python string, returns (success, output/error) — thread-safe
    std::pair<bool, std::string> run_string(const std::string& code);

    /// Set execution timeout in seconds (0 = no timeout)
    void set_timeout(double seconds) { timeout_seconds_ = seconds; }

    /// Cancellation flag — checked by lvv_module functions to bail out early.
    /// Per-instance, but the active engine is set via set_active() so
    /// lvv_module can find the right flag.
    std::atomic<bool>& cancelled() { return cancelled_; }

    /// Set this engine as the active one (for lvv_module to query cancellation)
    static void set_active(ScriptEngine* engine);
    static ScriptEngine* active();

private:
    void thread_main();

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_request_;
    std::condition_variable cv_done_;

    // Work item
    std::string pending_code_;
    std::pair<bool, std::string> result_;
    bool has_work_ = false;
    bool work_done_ = false;
    bool busy_ = false;  // true while script thread is executing
    bool shutdown_ = false;
    double timeout_seconds_ = 30.0;
    std::atomic<bool> cancelled_{false};

    // Per-VM context for the lvv Python module (set before script execution)
    LvvModuleContext module_ctx_;
};

} // namespace lvv
