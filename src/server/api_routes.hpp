#pragma once

#include <crow_all.h>

namespace lvv {

using CrowApp = crow::App<crow::CORSHandler>;

class Protocol;
class WidgetTree;
class WSHandler;
class ScriptEngine;
class TestRunner;

/// Register all REST API routes
void register_api_routes(CrowApp& app,
                         Protocol* protocol,
                         WidgetTree* tree,
                         WSHandler* ws,
                         ScriptEngine* script_engine,
                         TestRunner* test_runner,
                         const std::string& ref_images_dir = "ref_images");

} // namespace lvv
