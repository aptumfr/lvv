#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace lvv {

struct ProcessResult {
    int exit_code = 1;
    std::string output;
    bool timed_out = false;
};

/// Run a subprocess with captured output and timeout enforcement.
/// Platform-independent interface; uses fork/exec on POSIX, CreateProcess on Windows.
/// No shell is involved — arguments are passed directly.
class ProcessRunner {
public:
    ProcessRunner& executable(const std::string& exe) { exe_ = exe; return *this; }
    ProcessRunner& arg(const std::string& a) { args_.push_back(a); return *this; }
    ProcessRunner& env(const std::string& key, const std::string& value) {
        env_[key] = value;
        return *this;
    }
    ProcessRunner& timeout(std::chrono::duration<double> t) { timeout_ = t; return *this; }

    /// Execute the process. Blocks until completion or timeout.
    ProcessResult run() const;

private:
#ifndef _WIN32
    ProcessResult run_posix() const;
#else
    ProcessResult run_win32() const;
#endif

    std::string exe_;
    std::vector<std::string> args_;
    std::map<std::string, std::string> env_;
    std::chrono::duration<double> timeout_{30.0};
};

/// Probe whether a TCP port on localhost is accepting connections.
/// Retries for up to `timeout` with 50ms intervals.
bool wait_for_port(int port, std::chrono::duration<double> timeout = std::chrono::seconds(2));

/// RAII Winsock initializer. No-op on non-Windows.
/// Instantiate before any socket use (find_free_port, wait_for_port, etc.)
struct WinsockInit {
#ifdef _WIN32
    WinsockInit();
    ~WinsockInit();
    WinsockInit(const WinsockInit&) = delete;
    WinsockInit& operator=(const WinsockInit&) = delete;
#endif
};

} // namespace lvv
