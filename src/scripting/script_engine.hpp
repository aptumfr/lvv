#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace lvv {

class Protocol;

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

    /// Run a Python file, returns (success, output/error)
    std::pair<bool, std::string> run_file(const std::string& path);

    /// Run a Python string, returns (success, output/error) — thread-safe
    std::pair<bool, std::string> run_string(const std::string& code);

    /// Set execution timeout in seconds (0 = no timeout)
    void set_timeout(double seconds) { timeout_seconds_ = seconds; }

    /// Cancellation flag — checked by lvv_module functions to bail out early
    static std::atomic<bool>& cancelled();

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
};

} // namespace lvv
