#include "app.hpp"
#include "transport/tcp_transport.hpp"
#include "transport/serial_transport.hpp"
#include "core/screen_capture.hpp"
#include "core/junit_xml.hpp"
#include "scripting/lvv_module.hpp"

#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <csignal>

namespace lvv {

static std::atomic<bool> s_running{true};

static void signal_handler(int) {
    s_running = false;
}

App::App() = default;
App::~App() = default;

bool App::connect(const AppConfig& config) {
    if (!config.serial_device.empty()) {
        transport_ = std::make_unique<SerialTransport>(
            config.serial_device, config.serial_baud);
        std::cout << "Connecting via serial: " << config.serial_device
                  << " @ " << config.serial_baud << " baud\n";
    } else {
        transport_ = std::make_unique<TCPTransport>(
            config.target_host, config.target_port);
        std::cout << "Connecting to " << config.target_host
                  << ":" << config.target_port << "\n";
    }

    protocol_ = std::make_unique<Protocol>(transport_.get());

    if (!protocol_->connect()) {
        std::cerr << "Failed to connect\n";
        return false;
    }
    return true;
}

int App::ping(const AppConfig& config) {
    if (!connect(config)) return 1;

    try {
        auto version = protocol_->ping();
        std::cout << "Connected. Spy version: " << version << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Ping failed: " << e.what() << "\n";
        return 1;
    }
}

int App::tree(const AppConfig& config, bool auto_paths) {
    if (!connect(config)) return 1;

    try {
        auto tree_json = protocol_->get_tree();
        widget_tree_ = std::make_unique<WidgetTree>();
        widget_tree_->update(tree_json);

        std::cout << widget_tree_->to_json().dump(2) << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Failed to get tree: " << e.what() << "\n";
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
            std::cout << "Screenshot saved to " << out_path << "\n";
            return 0;
        } else {
            std::cerr << "Failed to save screenshot\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Screenshot failed: " << e.what() << "\n";
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
        std::cerr << "No test files found\n";
        return 1;
    }

    std::cout << "Running " << files.size() << " test(s)...\n";

    auto suite = test_runner_->run_suite("lvv_tests", files);

    // Print summary
    std::cout << "\n"
              << suite.passed() << " passed, "
              << suite.failed() << " failed, "
              << suite.tests.size() << " total ("
              << suite.total_duration_seconds << "s)\n";

    // Write JUnit XML
    if (!config.junit_output.empty()) {
        auto xml = generate_junit_xml(suite);
        std::ofstream f(config.junit_output);
        if (f.is_open()) {
            f << xml;
            std::cout << "JUnit report: " << config.junit_output << "\n";
        }
    }

    return suite.all_passed() ? 0 : 1;
}

int App::serve(const AppConfig& config) {
    if (!connect(config)) {
        std::cerr << "Warning: Could not connect to target. "
                  << "Server starting anyway.\n";
    }

    script_engine_ = std::make_unique<ScriptEngine>();
    if (protocol_) {
        script_engine_->set_protocol(protocol_.get());
    }

    widget_tree_ = std::make_unique<WidgetTree>();
    test_runner_ = std::make_unique<TestRunner>(*script_engine_);

    web_server_ = std::make_unique<WebServer>(
        protocol_.get(), widget_tree_.get(),
        script_engine_.get(), test_runner_.get(),
        config.static_dir);

    // Handle Ctrl-C
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "LVV Server v0.1.0\n";
    if (!config.serial_device.empty()) {
        std::cout << "Target: " << config.serial_device
                  << " @ " << config.serial_baud << " baud\n";
    } else {
        std::cout << "Target: " << config.target_host
                  << ":" << config.target_port << "\n";
    }

    web_server_->start(config.web_port, false);

    return 0;
}

} // namespace lvv
