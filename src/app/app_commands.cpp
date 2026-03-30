/// One-shot CLI commands: doctor, ping, tree, screenshot

#include "app.hpp"
#include "protocol/protocol.hpp"
#include "core/widget_tree.hpp"
#include "core/screen_capture.hpp"
#include "core/log.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace lvv {

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

    auto ref_dir = std::filesystem::absolute(config.ref_images_dir);
    check("Ref images dir", true, ref_dir.string()
          + (std::filesystem::is_directory(ref_dir) ? " (exists)" : " (will be created)"));

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

        std::cout << widget_tree_->to_json().dump(2) << "\n";
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

} // namespace lvv
