#include "app.hpp"
#include "transport/tcp_transport.hpp"
#include "transport/serial_transport.hpp"
#include "protocol/protocol.hpp"
#include "core/widget_tree.hpp"
#include "core/screen_capture.hpp"
#include "core/test_runner.hpp"
#include "core/test_result.hpp"
#include "core/junit_xml.hpp"
#include "core/html_report.hpp"
#include "core/process_runner.hpp"
#include "scripting/script_engine.hpp"
#include "scripting/lvv_module.hpp"
#include "server/web_server.hpp"
#include "core/log.hpp"

#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstdlib>

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

int App::doctor(const AppConfig& config) {
    int issues = 0;

    auto check = [&](const char* label, bool ok, const std::string& detail = "") {
        if (ok) {
            LOG_INFO(log::get(), "  [OK]   {}{}", label, detail.empty() ? "" : " — " + detail);
        } else {
            LOG_ERROR(log::get(), "  [FAIL] {}{}", label, detail.empty() ? "" : " — " + detail);
            issues++;
        }
    };

    LOG_INFO(log::get(), "LVV Doctor — checking setup...");
    LOG_INFO(log::get(), "");

    // Target connectivity
    bool connected = false;
    std::string spy_version;
    try {
        if (connect(config)) {
            connected = true;
            spy_version = protocol_->ping();
        }
    } catch (...) {}

    if (!config.serial_device.empty()) {
        check("Serial device", connected,
              config.serial_device + " @ " + std::to_string(config.serial_baud));
    } else {
        check("Target connection", connected,
              config.target_host + ":" + std::to_string(config.target_port));
    }
    check("Spy version", !spy_version.empty(), spy_version);

    // Screen info
    if (connected) {
        try {
            auto info = protocol_->get_screen_info();
            check("Display", info.width > 0,
                  std::to_string(info.width) + "x" + std::to_string(info.height)
                  + " " + info.color_format);
        } catch (...) {
            check("Display", false, "failed to query");
        }
    }

    // Reference images directory
    auto ref_dir = std::filesystem::absolute(config.ref_images_dir);
    check("Ref images dir", true, ref_dir.string()
          + (std::filesystem::is_directory(ref_dir) ? " (exists)" : " (will be created)"));

    // Python (for --python mode)
    auto python_dir = find_lvv_python_dir();
    check("lvv.py module", !python_dir.empty(),
          python_dir.empty() ? "not found" : python_dir);

    std::string python_exe;
#ifdef _WIN32
    python_exe = "python";
#else
    python_exe = "python3";
#endif
#ifdef _WIN32
    bool python_ok = (std::system((python_exe + " --version > NUL 2>&1").c_str()) == 0);
#else
    bool python_ok = (std::system((python_exe + " --version > /dev/null 2>&1").c_str()) == 0);
#endif
    check("System Python", python_ok,
          python_ok ? python_exe : python_exe + " not found in PATH");

    // Web UI static files
    auto static_dir = default_static_dir();
    bool has_web = std::filesystem::is_directory(static_dir);
    check("Web UI", has_web,
          has_web ? static_dir : "not found (run npm build in web/)");

    LOG_INFO(log::get(), "");
    if (issues == 0) {
        LOG_INFO(log::get(), "All checks passed.");
    } else {
        LOG_ERROR(log::get(), "{} issue(s) found.", issues);
    }

    return issues > 0 ? 1 : 0;
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

// Write test reports (JUnit XML and/or HTML) if output paths are configured
static void write_reports(const TestSuiteResult& suite, const AppConfig& config) {
    if (!config.junit_output.empty()) {
        std::ofstream f(config.junit_output);
        if (f.is_open()) {
            f << generate_junit_xml(suite);
            LOG_INFO(log::get(), "JUnit report: {}", config.junit_output);
        }
    }
    if (!config.html_output.empty()) {
        std::ofstream f(config.html_output);
        if (f.is_open()) {
            f << generate_html_report(suite);
            LOG_INFO(log::get(), "HTML report: {}", config.html_output);
        }
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
    test_runner_->set_fail_fast(config.fail_fast);
    if (!config.setup_script.empty()) {
        test_runner_->set_setup_script(
            std::filesystem::absolute(config.setup_script).string());
    }

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
    write_reports(suite, config);

    return suite.all_passed() ? 0 : 1;
}

// --- Helpers for run_tests_python ---

static std::string resolve_python_exe(const AppConfig& config) {
    if (!config.python_exe.empty()) return config.python_exe;
#ifdef _WIN32
    return "python";
#else
    return "python3";
#endif
}

static std::string build_pythonpath(const std::string& python_dir) {
    std::string pythonpath = python_dir;
    if (const char* existing = std::getenv("PYTHONPATH")) {
#ifdef _WIN32
        pythonpath += ";";
#else
        pythonpath += ":";
#endif
        pythonpath += existing;
    }
    return pythonpath;
}

static std::vector<std::string> collect_test_files(
    const std::vector<std::string>& paths) {
    std::vector<std::string> files;
    for (const auto& path : paths) {
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
    return files;
}

static TestResult run_one_test_python(
    const std::string& file, const std::string& python_exe,
    const std::map<std::string, std::string>& env, double timeout_secs) {

    auto start = std::chrono::steady_clock::now();
    auto name = std::filesystem::path(file).stem().string();

    auto proc_result = ProcessRunner()
        .executable(python_exe)
        .arg(file)
        .env("LVV_URL", env.at("LVV_URL"))
        .env("PYTHONPATH", env.at("PYTHONPATH"))
        .env("LVV_REF_IMAGES", env.at("LVV_REF_IMAGES"))
        .timeout(std::chrono::duration<double>(timeout_secs))
        .run();

    auto end = std::chrono::steady_clock::now();
    double duration = std::chrono::duration<double>(end - start).count();

    TestResult result;
    result.name = name;
    result.duration_seconds = duration;
    result.output = proc_result.output;

    if (proc_result.timed_out) {
        result.status = TestStatus::Fail;
        result.message = "Script execution timed out (" +
            std::to_string(static_cast<int>(timeout_secs)) + "s)";
    } else if (proc_result.exit_code == 0) {
        result.status = TestStatus::Pass;
    } else {
        result.status = TestStatus::Fail;
        result.message = proc_result.output;
    }
    return result;
}

int App::run_tests_python(const AppConfig& config) {
    [[maybe_unused]] WinsockInit winsock;  // no-op on Linux, required on Windows
    if (!connect(config)) return 1;

    auto python_dir = find_lvv_python_dir();
    if (python_dir.empty()) {
        LOG_ERROR(log::get(), "Cannot find lvv.py module");
        return 1;
    }

    auto python_exe = resolve_python_exe(config);

    // Start ephemeral HTTP server
    int api_port = find_free_port();
    if (api_port == 0) {
        LOG_ERROR(log::get(), "Cannot find a free port");
        return 1;
    }

    widget_tree_ = std::make_unique<WidgetTree>();
    lvv_module_set_defaults(config.ref_images_dir, config.diff_threshold);

    web_server_ = std::make_unique<WebServer>(
        protocol_.get(), widget_tree_.get(),
        nullptr, nullptr, "", config.ref_images_dir);
    web_server_->start(api_port, true);

    if (!wait_for_port(api_port)) {
        LOG_ERROR(log::get(), "Internal API server failed to start on port {}", api_port);
        web_server_->stop();
        return 1;
    }
    LOG_INFO(log::get(), "Internal API server on port {}", api_port);

    auto files = collect_test_files(config.test_files);
    if (files.empty()) {
        LOG_ERROR(log::get(), "No test files found");
        web_server_->stop();
        return 1;
    }

    LOG_INFO(log::get(), "Running {} test(s) with system Python...", files.size());

    std::map<std::string, std::string> env = {
        {"LVV_URL", "http://127.0.0.1:" + std::to_string(api_port)},
        {"PYTHONPATH", build_pythonpath(python_dir)},
        {"LVV_REF_IMAGES", std::filesystem::absolute(config.ref_images_dir).string()},
    };

    TestSuiteResult suite;
    suite.name = "lvv_tests";
    auto suite_start = std::chrono::steady_clock::now();

    std::string setup_abs;
    if (!config.setup_script.empty()) {
        setup_abs = std::filesystem::absolute(config.setup_script).string();
    }

    for (const auto& file : files) {
        // Run setup script before each test for isolation
        if (!setup_abs.empty()) {
            auto setup_result = run_one_test_python(
                setup_abs, python_exe, env, config.timeout);
            // Setup failures are warnings, not test failures
        }
        suite.tests.push_back(
            run_one_test_python(file, python_exe, env, config.timeout));
        if (config.fail_fast && suite.tests.back().status != TestStatus::Pass) {
            break;
        }
    }

    suite.total_duration_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - suite_start).count();

    LOG_INFO(log::get(), "{} passed, {} failed, {} total ({:.2f}s)",
             suite.passed(), suite.failed(), suite.tests.size(),
             suite.total_duration_seconds);

    write_reports(suite, config);

    web_server_->stop();
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
