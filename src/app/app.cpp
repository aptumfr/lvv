#include "app.hpp"
#include "transport/tcp_transport.hpp"
#include "transport/serial_transport.hpp"
#include "protocol/protocol.hpp"
#include "core/widget_tree.hpp"
#include "core/screen_capture.hpp"
#include "core/test_runner.hpp"
#include "core/junit_xml.hpp"
#include "scripting/script_engine.hpp"
#include "scripting/lvv_module.hpp"
#include "server/web_server.hpp"
#include "core/log.hpp"

#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace lvv {

App::App() = default;
App::~App() = default;

bool App::connect(const AppConfig& config) {
    if (!config.serial_device.empty()) {
        transport_ = std::make_unique<SerialTransport>(
            config.serial_device, config.serial_baud);
        LOG_INFO(log::get(), "Connecting via serial: {} @ {} baud",
                 config.serial_device, config.serial_baud);
    } else {
        transport_ = std::make_unique<TCPTransport>(
            config.target_host, config.target_port);
        LOG_INFO(log::get(), "Connecting to {}:{}", config.target_host, config.target_port);
    }

    protocol_ = std::make_unique<Protocol>(transport_.get());

    if (!protocol_->connect()) {
        LOG_ERROR(log::get(), "Failed to connect");
        return false;
    }
    return true;
}

int App::ping(const AppConfig& config) {
    if (!connect(config)) return 1;

    try {
        auto version = protocol_->ping();
        LOG_INFO(log::get(), "Connected. Spy version: {}", version);
        return 0;
    } catch (const std::exception& e) {
        LOG_ERROR(log::get(), "Ping failed: {}", e.what());
        return 1;
    }
}

int App::tree(const AppConfig& config, bool auto_paths) {
    if (!connect(config)) return 1;

    try {
        auto tree_json = protocol_->get_tree();
        widget_tree_ = std::make_unique<WidgetTree>();
        widget_tree_->update(tree_json);

        std::cout << widget_tree_->to_json().dump(2) << "\n";  // stdout for piping
        return 0;
    } catch (const std::exception& e) {
        LOG_ERROR(log::get(), "Failed to get tree: {}", e.what());
        return 1;
    }
}

int App::screenshot(const AppConfig& config, const std::string& output) {
    if (!connect(config)) return 1;

    try {
        auto img = protocol_->screenshot();
        if (!img.valid()) {
            std::cerr << "Failed to capture screenshot\n";
            return 1;
        }

        std::string out_path = output.empty() ? "screenshot.png" : output;
        if (save_png(img, out_path)) {
            LOG_INFO(log::get(), "Screenshot saved to {}", out_path);
            return 0;
        } else {
            LOG_ERROR(log::get(), "Failed to save screenshot");
            return 1;
        }
    } catch (const std::exception& e) {
        LOG_ERROR(log::get(), "Screenshot failed: {}", e.what());
        return 1;
    }
}

int App::run_tests(const AppConfig& config) {
    if (config.use_system_python) {
        return run_tests_python(config);
    }
    if (!connect(config)) return 1;

    script_engine_ = std::make_unique<ScriptEngine>();
    script_engine_->set_protocol(protocol_.get());

    // Forward CLI settings to the Python module
    lvv_module_set_defaults(config.ref_images_dir, config.diff_threshold);

    test_runner_ = std::make_unique<TestRunner>(*script_engine_);
    test_runner_->set_timeout(config.timeout);
    test_runner_->set_verbose(config.verbose);

    // Collect test files
    std::vector<std::string> files;
    for (const auto& path : config.test_files) {
        if (std::filesystem::is_directory(path)) {
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.path().extension() == ".py") {
                    files.push_back(entry.path().string());
                }
            }
        } else {
            files.push_back(path);
        }
    }

    std::sort(files.begin(), files.end());

    if (files.empty()) {
        LOG_ERROR(log::get(), "No test files found");
        return 1;
    }

    LOG_INFO(log::get(), "Running {} test(s)...", files.size());

    auto suite = test_runner_->run_suite("lvv_tests", files);

    // Print summary
    LOG_INFO(log::get(), "{} passed, {} failed, {} total ({:.2f}s)",
             suite.passed(), suite.failed(), suite.tests.size(),
             suite.total_duration_seconds);

    // Write JUnit XML
    if (!config.junit_output.empty()) {
        auto xml = generate_junit_xml(suite);
        std::ofstream f(config.junit_output);
        if (f.is_open()) {
            f << xml;
            LOG_INFO(log::get(), "JUnit report: {}", config.junit_output);
        }
    }

    return suite.all_passed() ? 0 : 1;
}

int App::run_tests_python(const AppConfig& config) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    if (!connect(config)) return 1;

    // Find lvv.py module
    auto python_dir = find_lvv_python_dir();
    if (python_dir.empty()) {
        LOG_ERROR(log::get(), "Cannot find lvv.py module");
        return 1;
    }

    // Detect Python executable
    std::string python_exe = config.python_exe;
    if (python_exe.empty()) {
#ifdef _WIN32
        python_exe = "python";
#else
        python_exe = "python3";
#endif
    }

    // Start ephemeral HTTP server
    int api_port = find_free_port();
    if (api_port == 0) {
        LOG_ERROR(log::get(), "Cannot find a free port for internal API server");
        return 1;
    }

    widget_tree_ = std::make_unique<WidgetTree>();
    lvv_module_set_defaults(config.ref_images_dir, config.diff_threshold);

    web_server_ = std::make_unique<WebServer>(
        protocol_.get(), widget_tree_.get(),
        nullptr, nullptr,
        "", config.ref_images_dir);
    web_server_->start(api_port, true);

    std::string lvv_url = "http://127.0.0.1:" + std::to_string(api_port);

    // Wait for server to be ready by polling TCP connectivity
    bool server_ready = false;
    for (int i = 0; i < 40; ++i) {
        auto sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock >= 0) {
            struct sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(api_port);
            bool ok = (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0);
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
            if (ok) { server_ready = true; break; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!server_ready) {
        LOG_ERROR(log::get(), "Internal API server failed to start on port {}", api_port);
        web_server_->stop();
        return 1;
    }

    LOG_INFO(log::get(), "Internal API server on port {}", api_port);

    // Collect test files
    std::vector<std::string> files;
    for (const auto& path : config.test_files) {
        if (std::filesystem::is_directory(path)) {
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.path().extension() == ".py") {
                    files.push_back(std::filesystem::absolute(entry.path()).string());
                }
            }
        } else {
            files.push_back(std::filesystem::absolute(path).string());
        }
    }
    std::sort(files.begin(), files.end());

    if (files.empty()) {
        LOG_ERROR(log::get(), "No test files found");
        return 1;
    }

    LOG_INFO(log::get(), "Running {} test(s) with system Python...", files.size());

    // Build environment variables for subprocess
    std::string pythonpath = python_dir;
    if (const char* existing = std::getenv("PYTHONPATH")) {
#ifdef _WIN32
        pythonpath += ";" + std::string(existing);
#else
        pythonpath += ":" + std::string(existing);
#endif
    }

    // Pass ref_images dir so the API server uses the right path
    auto ref_images_abs = std::filesystem::absolute(config.ref_images_dir).string();

    TestSuiteResult suite;
    suite.name = "lvv_tests";
    auto suite_start = std::chrono::steady_clock::now();

    for (const auto& file : files) {
        auto test_start = std::chrono::steady_clock::now();
        auto name = std::filesystem::path(file).stem().string();

        // Run subprocess without shell (fork/exec on Unix)
        std::string output;
        int exit_code = 1;
        bool timed_out = false;

#ifndef _WIN32
        // Create pipe for stdout+stderr capture
        int pipefd[2];
        if (pipe(pipefd) != 0) {
            suite.tests.push_back({name, TestStatus::Error, 0, "pipe() failed", ""});
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            close(pipefd[0]); close(pipefd[1]);
            suite.tests.push_back({name, TestStatus::Error, 0, "fork() failed", ""});
            continue;
        }

        if (pid == 0) {
            // Child: redirect stdout+stderr to pipe, exec python
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);

            setenv("LVV_URL", lvv_url.c_str(), 1);
            setenv("PYTHONPATH", pythonpath.c_str(), 1);
            setenv("LVV_REF_IMAGES", ref_images_abs.c_str(), 1);

            execlp(python_exe.c_str(), python_exe.c_str(), file.c_str(), nullptr);
            _exit(127);  // exec failed
        }

        // Parent: read output and enforce timeout
        close(pipefd[1]);
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::duration<double>(config.timeout);

        // Non-blocking read with timeout
        fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
        while (true) {
            char buf[4096];
            ssize_t n = read(pipefd[0], buf, sizeof(buf));
            if (n > 0) {
                output.append(buf, n);
            } else if (n == 0) {
                break;  // EOF — child closed pipe
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Check timeout
                if (std::chrono::steady_clock::now() >= deadline) {
                    kill(pid, SIGTERM);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    kill(pid, SIGKILL);
                    timed_out = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } else {
                break;  // read error
            }
        }
        close(pipefd[0]);

        int wstatus;
        waitpid(pid, &wstatus, 0);
        exit_code = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 1;
#else
        // Windows: use CreateProcess with pipe capture and timeout
        _putenv_s("LVV_URL", lvv_url.c_str());
        _putenv_s("PYTHONPATH", pythonpath.c_str());
        _putenv_s("LVV_REF_IMAGES", ref_images_abs.c_str());

        SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
        HANDLE hReadPipe, hWritePipe;
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            suite.tests.push_back({name, TestStatus::Error, 0, "CreatePipe failed", ""});
            continue;
        }
        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si = { sizeof(si) };
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hWritePipe;
        si.hStdError = hWritePipe;
        PROCESS_INFORMATION pi = {};

        // Build command line without shell interpretation
        std::string cmdline = "\"" + python_exe + "\" \"" + file + "\"";
        std::vector<char> cmdline_buf(cmdline.begin(), cmdline.end());
        cmdline_buf.push_back('\0');

        BOOL ok = CreateProcessA(nullptr, cmdline_buf.data(), nullptr, nullptr,
                                  TRUE, 0, nullptr, nullptr, &si, &pi);
        CloseHandle(hWritePipe);

        if (!ok) {
            CloseHandle(hReadPipe);
            suite.tests.push_back({name, TestStatus::Error, 0, "CreateProcess failed", ""});
            continue;
        }

        // Read output with timeout
        DWORD timeout_ms = static_cast<DWORD>(config.timeout * 1000);
        auto win_deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(timeout_ms);
        while (true) {
            DWORD avail = 0;
            PeekNamedPipe(hReadPipe, nullptr, 0, nullptr, &avail, nullptr);
            if (avail > 0) {
                std::array<char, 4096> buf;
                DWORD nread = 0;
                ReadFile(hReadPipe, buf.data(),
                         std::min(avail, (DWORD)buf.size()), &nread, nullptr);
                if (nread > 0) output.append(buf.data(), nread);
            } else {
                DWORD wait = WaitForSingleObject(pi.hProcess, 10);
                if (wait == WAIT_OBJECT_0) break;  // process exited
                if (std::chrono::steady_clock::now() >= win_deadline) {
                    TerminateProcess(pi.hProcess, 1);
                    timed_out = true;
                    break;
                }
            }
        }
        // Drain remaining output
        while (true) {
            DWORD avail = 0;
            PeekNamedPipe(hReadPipe, nullptr, 0, nullptr, &avail, nullptr);
            if (avail == 0) break;
            std::array<char, 4096> buf;
            DWORD nread = 0;
            ReadFile(hReadPipe, buf.data(),
                     std::min(avail, (DWORD)buf.size()), &nread, nullptr);
            if (nread > 0) output.append(buf.data(), nread);
        }

        DWORD win_exit = 1;
        GetExitCodeProcess(pi.hProcess, &win_exit);
        exit_code = static_cast<int>(win_exit);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hReadPipe);
#endif

        auto test_end = std::chrono::steady_clock::now();
        double duration = std::chrono::duration<double>(test_end - test_start).count();

        TestResult result;
        result.name = name;
        result.duration_seconds = duration;
        if (timed_out) {
            result.status = TestStatus::Fail;
            result.message = "Script execution timed out (" +
                std::to_string(static_cast<int>(config.timeout)) + "s)";
            result.output = output;
        } else if (exit_code == 0) {
            result.status = TestStatus::Pass;
            result.output = output;
        } else {
            result.status = TestStatus::Fail;
            result.message = output;
            result.output = output;
        }
        suite.tests.push_back(std::move(result));
    }

    auto suite_end = std::chrono::steady_clock::now();
    suite.total_duration_seconds = std::chrono::duration<double>(suite_end - suite_start).count();

    LOG_INFO(log::get(), "{} passed, {} failed, {} total ({:.2f}s)",
             suite.passed(), suite.failed(), suite.tests.size(),
             suite.total_duration_seconds);

    if (!config.junit_output.empty()) {
        auto xml = generate_junit_xml(suite);
        std::ofstream f(config.junit_output);
        if (f.is_open()) {
            f << xml;
            LOG_INFO(log::get(), "JUnit report: {}", config.junit_output);
        }
    }

    web_server_->stop();
#ifdef _WIN32
    WSACleanup();
#endif
    return suite.all_passed() ? 0 : 1;
}

int App::serve(const AppConfig& config) {
    if (!connect(config)) {
        LOG_WARNING(log::get(), "Could not connect to target. Server starting anyway.");
    }

    script_engine_ = std::make_unique<ScriptEngine>();
    script_engine_->set_timeout(config.timeout);
    if (protocol_) {
        script_engine_->set_protocol(protocol_.get());
    }

    // Forward visual-test defaults so API-triggered scripts use the same settings as CLI
    lvv_module_set_defaults(config.ref_images_dir, config.diff_threshold);

    widget_tree_ = std::make_unique<WidgetTree>();
    test_runner_ = std::make_unique<TestRunner>(*script_engine_);

    web_server_ = std::make_unique<WebServer>(
        protocol_.get(), widget_tree_.get(),
        script_engine_.get(), test_runner_.get(),
        config.static_dir, config.ref_images_dir);

    LOG_INFO(log::get(), "LVV Server v0.1.0");
    if (!config.serial_device.empty()) {
        LOG_INFO(log::get(), "Target: {} @ {} baud", config.serial_device, config.serial_baud);
    } else {
        LOG_INFO(log::get(), "Target: {}:{}", config.target_host, config.target_port);
    }

    web_server_->start(config.web_port, false);

    return 0;
}

} // namespace lvv
