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
        config.static_dir);

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
