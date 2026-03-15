#include "script_engine.hpp"
#include "lvv_module.hpp"

#include "pocketpy.h"

#include <fstream>
#include <sstream>
#include <string>

namespace lvv {

static std::atomic<bool> s_cancelled{false};

std::atomic<bool>& ScriptEngine::cancelled() {
    return s_cancelled;
}

// Captured output from PocketPy print callback
static std::string s_captured_output;

static void capture_print(const char* text) {
    s_captured_output += text;
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
        s_captured_output.clear();
        lvv_module_reset_state();

        py_Callbacks* cb = py_callbacks();
        auto old_print = cb->print;
        cb->print = capture_print;

        bool success = py_exec(code.c_str(), "<test>", EXEC_MODE, nullptr);
        std::pair<bool, std::string> result;

        if (!success) {
            char* err = py_formatexc();
            std::string error_msg = err ? err : "Unknown error";
            if (err) PK_FREE(err);
            py_clearexc(nullptr);
            result = {false, error_msg};
        } else {
            result = {true, s_captured_output};
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
    lvv_module_set_protocol(protocol);
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

    s_cancelled.store(false);
    pending_code_ = code;
    has_work_ = true;
    work_done_ = false;
    cv_request_.notify_one();

    // Wait with timeout
    if (timeout_seconds_ > 0) {
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::duration<double>(timeout_seconds_);
        if (!cv_done_.wait_until(lock, deadline, [this] { return work_done_; })) {
            s_cancelled.store(true);  // signal lvv_module functions to bail out
            return {false, "Script execution timed out (" +
                    std::to_string(static_cast<int>(timeout_seconds_)) + "s)"};
        }
    } else {
        cv_done_.wait(lock, [this] { return work_done_; });
    }
    return std::move(result_);
}

} // namespace lvv
