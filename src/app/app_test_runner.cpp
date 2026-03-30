/// Test execution: run_tests (PocketPy) and run_tests_python (system Python)

#include "app.hpp"
#include "protocol/protocol.hpp"
#include "core/widget_tree.hpp"
#include "core/test_runner.hpp"
#include "core/test_result.hpp"
#include "core/junit_xml.hpp"
#include "core/html_report.hpp"
#include "core/process_runner.hpp"
#include "scripting/script_engine.hpp"
#include "server/web_server.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace lvv {

// --- Helpers (file-local) ---

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

    const auto start = std::chrono::steady_clock::now();
    const auto name = std::filesystem::path(file).stem().string();

    auto proc_result = ProcessRunner()
        .executable(python_exe)
        .arg(file)
        .env("LVV_URL", env.at("LVV_URL"))
        .env("PYTHONPATH", env.at("PYTHONPATH"))
        .env("LVV_REF_IMAGES", env.at("LVV_REF_IMAGES"))
        .timeout(std::chrono::duration<double>(timeout_secs))
        .run();

    const auto end = std::chrono::steady_clock::now();
    const double duration = std::chrono::duration<double>(end - start).count();

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

// --- Public methods ---

int App::run_tests(const AppConfig& config) {
    if (config.use_system_python) {
        return run_tests_python(config);
    }
    if (!connect(config)) return 1;

    script_engine_ = std::make_unique<ScriptEngine>();
    script_engine_->set_protocol(protocol_.get());
    script_engine_->set_visual_defaults(config.ref_images_dir, config.diff_threshold);

    test_runner_ = std::make_unique<TestRunner>(*script_engine_);
    test_runner_->set_timeout(config.timeout);
    test_runner_->set_verbose(config.verbose);
    test_runner_->set_fail_fast(config.fail_fast);
    if (!config.setup_script.empty()) {
        test_runner_->set_setup_script(
            std::filesystem::absolute(config.setup_script).string());
    }

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

    LOG_INFO(log::get(), "{} passed, {} failed, {} total ({:.2f}s)",
             suite.passed(), suite.failed(), suite.tests.size(),
             suite.total_duration_seconds);

    write_reports(suite, config);
    return suite.all_passed() ? 0 : 1;
}

int App::run_tests_python(const AppConfig& config) {
    [[maybe_unused]] const WinsockInit winsock;
    if (!connect(config)) return 1;

    auto python_dir = find_lvv_python_dir();
    if (python_dir.empty()) {
        LOG_ERROR(log::get(), "Cannot find lvv.py module");
        return 1;
    }

    const auto python_exe = resolve_python_exe(config);

    const int api_port = find_free_port();
    if (api_port == 0) {
        LOG_ERROR(log::get(), "Cannot find a free port");
        return 1;
    }

    widget_tree_ = std::make_unique<WidgetTree>();

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

    const std::map<std::string, std::string> env = {
        {"LVV_URL", "http://127.0.0.1:" + std::to_string(api_port)},
        {"PYTHONPATH", build_pythonpath(python_dir)},
        {"LVV_REF_IMAGES", std::filesystem::absolute(config.ref_images_dir).string()},
    };

    TestSuiteResult suite;
    suite.name = "lvv_tests";
    const auto suite_start = std::chrono::steady_clock::now();

    std::string setup_abs;
    if (!config.setup_script.empty()) {
        setup_abs = std::filesystem::absolute(config.setup_script).string();
    }

    for (const auto& file : files) {
        if (!setup_abs.empty()) {
            run_one_test_python(setup_abs, python_exe, env, config.timeout);
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

} // namespace lvv
