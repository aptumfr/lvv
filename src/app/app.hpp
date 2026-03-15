#pragma once

#include "transport/transport.hpp"
#include "protocol/protocol.hpp"
#include "core/widget_tree.hpp"
#include "core/test_runner.hpp"
#include "scripting/script_engine.hpp"
#include "server/web_server.hpp"

#include <memory>
#include <string>
#include <vector>

namespace lvv {

struct AppConfig {
    // Target connection
    std::string target_host = "localhost";
    int target_port = 5555;

    // Serial connection
    std::string serial_device;
    int serial_baud = 115200;

    // Web server
    int web_port = 8080;
#ifdef LVV_SOURCE_DIR
    std::string static_dir = LVV_SOURCE_DIR "/web/dist";
#else
    std::string static_dir = "web/dist";
#endif

    // Test execution
    std::vector<std::string> test_files;
    std::string junit_output;
    std::string ref_images_dir = "ref_images";
    double diff_threshold = 0.1;
    double timeout = 30.0;
    bool verbose = false;
};

class App {
public:
    App();
    ~App();

    /// Run in server mode (web UI)
    int serve(const AppConfig& config);

    /// Run tests headless
    int run_tests(const AppConfig& config);

    /// One-shot: ping the target
    int ping(const AppConfig& config);

    /// One-shot: dump widget tree
    int tree(const AppConfig& config, bool auto_paths);

    /// One-shot: take screenshot
    int screenshot(const AppConfig& config, const std::string& output);

private:
    bool connect(const AppConfig& config);

    std::unique_ptr<ITransport> transport_;
    std::unique_ptr<Protocol> protocol_;
    std::unique_ptr<ScriptEngine> script_engine_;
    std::unique_ptr<WidgetTree> widget_tree_;
    std::unique_ptr<TestRunner> test_runner_;
    std::unique_ptr<WebServer> web_server_;
};

} // namespace lvv
