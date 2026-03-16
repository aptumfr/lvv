#pragma once

#include "config.hpp"
#include <memory>
#include <string>

namespace lvv {

class ITransport;
class Protocol;
class ScriptEngine;
class WidgetTree;
class TestRunner;
class WebServer;

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
