#pragma once

#include "ws_handler.hpp"
#include <crow_all.h>
#include <string>
#include <thread>

namespace lvv {

using CrowApp = crow::App<crow::CORSHandler>;

class Protocol;
class WidgetTree;
class ScriptEngine;
class TestRunner;

class WebServer {
public:
    WebServer(Protocol* protocol, WidgetTree* tree,
              ScriptEngine* script_engine, TestRunner* test_runner,
              const std::string& static_dir = "web/dist");
    ~WebServer();

    void start(int port = 8080, bool async = true);
    void stop();

    WSHandler& ws_handler() { return ws_handler_; }

private:
    CrowApp app_;
    WSHandler ws_handler_;
    std::thread server_thread_;
    bool running_ = false;
    std::string static_dir_;
};

} // namespace lvv
