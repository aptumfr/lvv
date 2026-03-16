#include "app/app.hpp"
#include "core/log.hpp"
#include <CLI11.hpp>
#include <iostream>

int main(int argc, char** argv) {
    CLI::App cli{"LVV - LVGL Test Automation Tool"};
    cli.require_subcommand(1);

    lvv::AppConfig config;
    config.static_dir = lvv::default_static_dir();

    // Global connection options
    cli.add_option("--host", config.target_host, "Target host")
        ->default_val("localhost");
    cli.add_option("--port", config.target_port, "Target port")
        ->default_val(5555);
    cli.add_option("--serial", config.serial_device,
                   "Serial device (e.g. /dev/ttyUSB0)");
    cli.add_option("--baud", config.serial_baud, "Serial baud rate")
        ->default_val(115200);
    cli.add_flag("--verbose,-v", config.verbose, "Verbose output (debug logging)");

    // --- ping ---
    auto* ping_cmd = cli.add_subcommand("ping", "Ping the target");

    // --- tree ---
    auto* tree_cmd = cli.add_subcommand("tree", "Show widget tree");
    bool auto_paths = false;
    tree_cmd->add_flag("-a,--auto-paths", auto_paths, "Show auto-paths");

    // --- screenshot ---
    auto* screenshot_cmd = cli.add_subcommand("screenshot", "Capture screenshot");
    std::string screenshot_output;
    screenshot_cmd->add_option("-o,--output", screenshot_output, "Output file")
        ->default_val("screenshot.png");

    // --- run ---
    auto* run_cmd = cli.add_subcommand("run", "Run test file(s)");
    run_cmd->add_option("files", config.test_files, "Test files or directories")
        ->required();
    run_cmd->add_option("--output", config.junit_output, "JUnit XML output file");
    run_cmd->add_option("--ref-images", config.ref_images_dir,
                        "Reference images directory")
        ->default_val("ref_images");
    run_cmd->add_option("--threshold", config.diff_threshold,
                        "Visual diff threshold %")
        ->default_val(0.1);
    run_cmd->add_option("--timeout", config.timeout, "Per-test timeout (seconds)")
        ->default_val(30.0);
    // --- serve ---
    auto* serve_cmd = cli.add_subcommand("serve", "Start web UI server");
    serve_cmd->add_option("--web-port", config.web_port, "Web server port")
        ->default_val(8080);
    serve_cmd->add_option("--static-dir", config.static_dir,
                          "Static files directory (React build)");
    serve_cmd->add_option("--ref-images", config.ref_images_dir,
                          "Reference images directory")
        ->default_val("ref_images");
    serve_cmd->add_option("--threshold", config.diff_threshold,
                          "Visual diff threshold %")
        ->default_val(0.1);
    serve_cmd->add_option("--timeout", config.timeout, "Per-test timeout (seconds)")
        ->default_val(30.0);

    CLI11_PARSE(cli, argc, argv);

    lvv::log::init();
    if (config.verbose) {
        lvv::log::set_level(quill::LogLevel::Debug);
    }

    lvv::App app;

    if (ping_cmd->parsed()) {
        return app.ping(config);
    }
    if (tree_cmd->parsed()) {
        return app.tree(config, auto_paths);
    }
    if (screenshot_cmd->parsed()) {
        return app.screenshot(config, screenshot_output);
    }
    if (run_cmd->parsed()) {
        return app.run_tests(config);
    }
    if (serve_cmd->parsed()) {
        return app.serve(config);
    }

    return 0;
}
