#include "script_engine.hpp"
#include "lvv_module.hpp"

#include "pocketpy.h"

#include <fstream>
#include <sstream>
#include <string>

namespace lvv {

// Maps the current VM thread to its ScriptEngine for cancellation checking.
// Thread-local because PocketPy's pk_current_vm is also thread-local.
// Only one ScriptEngine exists per process (see LvvModuleContext invariant),
// but thread_local is correct if that ever changes.
static thread_local ScriptEngine* s_active_engine = nullptr;

void ScriptEngine::set_active(ScriptEngine* engine) {
    s_active_engine = engine;
}

ScriptEngine* ScriptEngine::active() {
    return s_active_engine;
}

// PocketPy print callback — appends to the current VM's captured output buffer
static void capture_print(const char* text) {
    auto* ctx = static_cast<LvvModuleContext*>(py_getvmctx());
    if (ctx) ctx->captured_output += text;
}

ScriptEngine::ScriptEngine() {
    thread_ = std::thread(&ScriptEngine::thread_main, this);
}

ScriptEngine::~ScriptEngine() {
    {
        std::lock_guard lock(mutex_);
        shutdown_ = true;
        cv_request_.notify_one();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void ScriptEngine::thread_main() {
    // Initialize PocketPy on THIS thread (pk_current_vm is _Thread_local)
    py_initialize();
    lvv_module_register();
    lvv_module_set_context(&module_ctx_);

    while (true) {
        std::unique_lock lock(mutex_);
        cv_request_.wait(lock, [this] { return has_work_ || shutdown_; });

        if (shutdown_ && !has_work_) break;

        // Execute the code
        std::string code = std::move(pending_code_);
        has_work_ = false;
        busy_ = true;
        lock.unlock();

        // Run outside the lock — reset state for isolation
        s_active_engine = this;
        module_ctx_.captured_output.clear();
        lvv_module_reset_state();

        py_Callbacks* cb = py_callbacks();
        auto old_print = cb->print;
        cb->print = capture_print;

        // Arm PocketPy's built-in watchdog to interrupt pure Python loops
        double timeout = timeout_seconds_;
        if (timeout > 0) {
            py_watchdog_begin(static_cast<int64_t>(timeout * 1000));
        }

        bool success = py_exec(code.c_str(), "<test>", EXEC_MODE, nullptr);

        if (timeout > 0) {
            py_watchdog_end();
        }

        std::pair<bool, std::string> result;

        if (!success) {
            char* err = py_formatexc();
            std::string error_msg = err ? err : "Unknown error";
            if (err) PK_FREE(err);
            py_clearexc(nullptr);
            result = {false, error_msg};
        } else {
            result = {true, module_ctx_.captured_output};
        }

        cb->print = old_print;

        lock.lock();
        busy_ = false;
        result_ = std::move(result);
        work_done_ = true;
        cv_done_.notify_one();
    }

    py_finalize();
}

void ScriptEngine::set_protocol(Protocol* protocol) {
    module_ctx_.protocol = protocol;
}

void ScriptEngine::set_visual_defaults(const std::string& ref_dir, double threshold) {
    module_ctx_.ref_images_dir = ref_dir;
    module_ctx_.default_threshold = threshold;
}

std::pair<bool, std::string> ScriptEngine::run_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        return {false, "Cannot open file: " + path};
    }

    std::ostringstream ss;
    ss << f.rdbuf();
    return run_string(ss.str());
}

std::pair<bool, std::string> ScriptEngine::run_string(const std::string& code) {
    std::unique_lock lock(mutex_);

    // If a previous script timed out and is still running, wait for it to drain
    if (busy_) {
        // Give the hung script a few seconds to finish naturally
        auto drain_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        if (!cv_done_.wait_until(lock, drain_deadline, [this] { return !busy_; })) {
            return {false, "Interpreter busy (previous script still running)"};
        }
    }

    cancelled_.store(false);
    pending_code_ = code;
    has_work_ = true;
    work_done_ = false;
    cv_request_.notify_one();

    // Wait with timeout
    if (timeout_seconds_ > 0) {
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::duration<double>(timeout_seconds_);
        if (!cv_done_.wait_until(lock, deadline, [this] { return work_done_; })) {
            cancelled_.store(true);  // signal lvv_module functions to bail out
            return {false, "Script execution timed out (" +
                    std::to_string(static_cast<int>(timeout_seconds_)) + "s)"};
        }
    } else {
        cv_done_.wait(lock, [this] { return work_done_; });
    }
    return std::move(result_);
}

} // namespace lvv
