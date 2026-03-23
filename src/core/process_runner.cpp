#include "process_runner.hpp"

#include <array>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace lvv {

// ============================================================
// RAII helpers
// ============================================================

#ifndef _WIN32

/// POSIX file descriptor — closes on destruction.
struct ScopedFd {
    int fd = -1;
    ScopedFd() = default;
    explicit ScopedFd(int f) : fd(f) {}
    ~ScopedFd() { if (fd >= 0) ::close(fd); }
    ScopedFd(ScopedFd&& o) noexcept : fd(o.fd) { o.fd = -1; }
    ScopedFd& operator=(ScopedFd&& o) noexcept {
        if (fd >= 0) ::close(fd);
        fd = o.fd; o.fd = -1;
        return *this;
    }
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;
    int release() { int f = fd; fd = -1; return f; }
    explicit operator bool() const { return fd >= 0; }
};

#else

/// Windows HANDLE — closes on destruction.
struct ScopedHandle {
    HANDLE h = INVALID_HANDLE_VALUE;
    ScopedHandle() = default;
    explicit ScopedHandle(HANDLE handle) : h(handle) {}
    ~ScopedHandle() { if (h != INVALID_HANDLE_VALUE && h != nullptr) CloseHandle(h); }
    ScopedHandle(ScopedHandle&& o) noexcept : h(o.h) { o.h = INVALID_HANDLE_VALUE; }
    ScopedHandle& operator=(ScopedHandle&& o) noexcept {
        if (h != INVALID_HANDLE_VALUE && h != nullptr) CloseHandle(h);
        h = o.h; o.h = INVALID_HANDLE_VALUE;
        return *this;
    }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    HANDLE release() { HANDLE tmp = h; h = INVALID_HANDLE_VALUE; return tmp; }
    explicit operator bool() const { return h != INVALID_HANDLE_VALUE && h != nullptr; }
};

/// Calls WSACleanup on destruction.
struct WinsockGuard {
    bool initialized = false;
    WinsockGuard() {
        WSADATA wsa;
        initialized = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    }
    ~WinsockGuard() { if (initialized) WSACleanup(); }
    WinsockGuard(const WinsockGuard&) = delete;
    WinsockGuard& operator=(const WinsockGuard&) = delete;
};

#endif

// ============================================================
// WinsockInit (public RAII wrapper)
// ============================================================

#ifdef _WIN32
WinsockInit::WinsockInit() { WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa); }
WinsockInit::~WinsockInit() { WSACleanup(); }
#endif

// ============================================================
// ProcessRunner::run()
// ============================================================

ProcessResult ProcessRunner::run() const {
#ifndef _WIN32
    return run_posix();
#else
    return run_win32();
#endif
}

// ============================================================
// POSIX implementation
// ============================================================

#ifndef _WIN32

ProcessResult ProcessRunner::run_posix() const {
    ProcessResult result;

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        result.output = "pipe() failed";
        return result;
    }
    ScopedFd read_end(pipefd[0]);
    ScopedFd write_end(pipefd[1]);

    pid_t pid = fork();
    if (pid < 0) {
        result.output = "fork() failed";
        return result;
    }

    if (pid == 0) {
        // Child
        read_end.release();  // don't close in child
        ::close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        ::close(pipefd[1]);

        for (const auto& [key, val] : env_) {
            setenv(key.c_str(), val.c_str(), 1);
        }

        // Build argv
        std::vector<const char*> argv;
        argv.push_back(exe_.c_str());
        for (const auto& a : args_) argv.push_back(a.c_str());
        argv.push_back(nullptr);

        execvp(exe_.c_str(), const_cast<char* const*>(argv.data()));
        _exit(127);
    }

    // Parent
    write_end = ScopedFd();  // close write end

    auto deadline = std::chrono::steady_clock::now() + timeout_;
    fcntl(read_end.fd, F_SETFL, O_NONBLOCK);

    while (true) {
        char buf[4096];
        ssize_t n = ::read(read_end.fd, buf, sizeof(buf));
        if (n > 0) {
            result.output.append(buf, n);
        } else if (n == 0) {
            break;  // EOF — child closed pipe
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (std::chrono::steady_clock::now() >= deadline) {
                kill(pid, SIGTERM);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                kill(pid, SIGKILL);
                result.timed_out = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            break;  // read error (not EAGAIN)
        }
    }

    read_end = ScopedFd();  // close before wait

    int wstatus;
    waitpid(pid, &wstatus, 0);
    result.exit_code = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 1;
    return result;
}

#endif

// ============================================================
// Windows implementation
// ============================================================

#ifdef _WIN32

// Build a NUL-separated, double-NUL-terminated environment block for CreateProcess.
// Inherits the parent environment and overrides/adds the given entries.
static std::vector<char> build_env_block(const std::map<std::string, std::string>& overrides) {
    // Collect parent environment
    std::map<std::string, std::string> merged;
    char* env_strings = GetEnvironmentStringsA();
    if (env_strings) {
        for (const char* p = env_strings; *p; p += strlen(p) + 1) {
            std::string entry(p);
            auto eq = entry.find('=');
            if (eq != std::string::npos && eq > 0) {
                merged[entry.substr(0, eq)] = entry.substr(eq + 1);
            }
        }
        FreeEnvironmentStringsA(env_strings);
    }
    // Apply overrides
    for (const auto& [k, v] : overrides) merged[k] = v;

    // Serialize: "KEY=VALUE\0KEY=VALUE\0\0"
    std::vector<char> block;
    for (const auto& [k, v] : merged) {
        std::string entry = k + "=" + v;
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back('\0');
    }
    block.push_back('\0');
    return block;
}

ProcessResult ProcessRunner::run_win32() const {
    ProcessResult result;

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        result.output = "CreatePipe failed";
        return result;
    }
    ScopedHandle read_pipe(hRead);
    ScopedHandle write_pipe(hWrite);
    SetHandleInformation(read_pipe.h, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_pipe.h;
    si.hStdError = write_pipe.h;
    PROCESS_INFORMATION pi = {};

    // Build command line
    std::string cmdline = "\"" + exe_ + "\"";
    for (const auto& a : args_) cmdline += " \"" + a + "\"";
    std::vector<char> cmdline_buf(cmdline.begin(), cmdline.end());
    cmdline_buf.push_back('\0');

    // Build isolated environment block (does not mutate parent process)
    auto env_block = build_env_block(env_);

    BOOL ok = CreateProcessA(nullptr, cmdline_buf.data(), nullptr, nullptr,
                              TRUE, 0, env_block.data(), nullptr, &si, &pi);
    write_pipe = ScopedHandle();  // close write end

    if (!ok) {
        result.output = "CreateProcess failed";
        return result;
    }

    ScopedHandle proc(pi.hProcess);
    ScopedHandle thread(pi.hThread);

    auto deadline = std::chrono::steady_clock::now() + timeout_;

    // Read output with timeout
    while (true) {
        DWORD avail = 0;
        PeekNamedPipe(read_pipe.h, nullptr, 0, nullptr, &avail, nullptr);
        if (avail > 0) {
            std::array<char, 4096> buf;
            DWORD nread = 0;
            ReadFile(read_pipe.h, buf.data(),
                     std::min(avail, (DWORD)buf.size()), &nread, nullptr);
            if (nread > 0) result.output.append(buf.data(), nread);
        } else {
            DWORD wait = WaitForSingleObject(proc.h, 10);
            if (wait == WAIT_OBJECT_0) break;
            if (std::chrono::steady_clock::now() >= deadline) {
                TerminateProcess(proc.h, 1);
                result.timed_out = true;
                break;
            }
        }
    }

    // Drain remaining
    while (true) {
        DWORD avail = 0;
        PeekNamedPipe(read_pipe.h, nullptr, 0, nullptr, &avail, nullptr);
        if (avail == 0) break;
        std::array<char, 4096> buf;
        DWORD nread = 0;
        ReadFile(read_pipe.h, buf.data(),
                 std::min(avail, (DWORD)buf.size()), &nread, nullptr);
        if (nread > 0) result.output.append(buf.data(), nread);
    }

    DWORD win_exit = 1;
    GetExitCodeProcess(proc.h, &win_exit);
    result.exit_code = static_cast<int>(win_exit);
    return result;
}

#endif

// ============================================================
// wait_for_port
// ============================================================

bool wait_for_port(int port, std::chrono::duration<double> timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
#ifdef _WIN32
        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }
#else
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }
#endif
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(static_cast<uint16_t>(port));

        bool ok = (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0);
#ifdef _WIN32
        closesocket(sock);
#else
        ::close(sock);
#endif
        if (ok) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

} // namespace lvv
