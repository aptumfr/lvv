#include "web_server.hpp"
#include "api_routes.hpp"
#include "protocol/protocol.hpp"
#include "core/widget_tree.hpp"
#include "core/test_runner.hpp"
#include "scripting/script_engine.hpp"

#include "core/log.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace lvv {

static std::string read_file_contents(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string mime_type(const std::string& path) {
    if (path.ends_with(".html")) return "text/html";
    if (path.ends_with(".js"))   return "application/javascript";
    if (path.ends_with(".css"))  return "text/css";
    if (path.ends_with(".json")) return "application/json";
    if (path.ends_with(".png"))  return "image/png";
    if (path.ends_with(".svg"))  return "image/svg+xml";
    if (path.ends_with(".ico"))  return "image/x-icon";
    if (path.ends_with(".woff")) return "font/woff";
    if (path.ends_with(".woff2")) return "font/woff2";
    if (path.ends_with(".map")) return "application/json";
    return "application/octet-stream";
}

WebServer::WebServer(Protocol* protocol, WidgetTree* tree,
                     ScriptEngine* script_engine, TestRunner* test_runner,
                     const std::string& static_dir,
                     const std::string& ref_images_dir)
    : static_dir_(static_dir)
{
    // CORS: allow all origins (needed for Vite dev server)
    auto& cors = app_.get_middleware<crow::CORSHandler>();
    cors.global()
        .origin("*")
        .methods("GET"_method, "POST"_method, "OPTIONS"_method)
        .headers("Content-Type");

    // Register REST API routes
    register_api_routes(app_, protocol, tree, &ws_handler_,
                        script_engine, test_runner, ref_images_dir);

    // Register WebSocket handler
    ws_handler_.setup(app_, protocol);

    // Serve index.html for root and SPA fallback
    CROW_ROUTE(app_, "/")
    ([this]() {
        auto content = read_file_contents(static_dir_ + "/index.html");
        if (content.empty()) {
            return crow::response(404, "index.html not found. Build the web UI first: cd web && npm run build");
        }
        crow::response resp(content);
        resp.set_header("Content-Type", "text/html");
        return resp;
    });

    // Serve static assets from the build directory
    CROW_ROUTE(app_, "/assets/<path>")
    ([this](const std::string& path) {
        auto file_path = static_dir_ + "/assets/" + path;

        // Prevent path traversal
        auto canonical = std::filesystem::weakly_canonical(file_path).string();
        auto base = std::filesystem::weakly_canonical(static_dir_).string() + "/";
        if (canonical.rfind(base, 0) != 0) {
            return crow::response(403, "Forbidden");
        }

        auto content = read_file_contents(file_path);
        if (content.empty()) {
            return crow::response(404, "Not found");
        }

        crow::response resp(content);
        resp.set_header("Content-Type", mime_type(file_path));
        resp.set_header("Cache-Control", "public, max-age=31536000, immutable");
        return resp;
    });

    // Serve other static files (favicon, etc.)
    CROW_ROUTE(app_, "/<path>")
    ([this](const std::string& path) {
        // Don't serve API or WS routes
        if (path.starts_with("api/") || path == "ws") {
            return crow::response(404);
        }

        auto file_path = static_dir_ + "/" + path;

        // Prevent path traversal
        auto canonical = std::filesystem::weakly_canonical(file_path).string();
        auto base = std::filesystem::weakly_canonical(static_dir_).string() + "/";
        if (canonical.rfind(base, 0) != 0) {
            return crow::response(403, "Forbidden");
        }

        auto content = read_file_contents(file_path);
        if (!content.empty()) {
            crow::response resp(content);
            resp.set_header("Content-Type", mime_type(file_path));
            return resp;
        }

        // SPA fallback: serve index.html for unknown paths
        content = read_file_contents(static_dir_ + "/index.html");
        if (content.empty()) {
            return crow::response(404);
        }
        crow::response resp(content);
        resp.set_header("Content-Type", "text/html");
        return resp;
    });
}

WebServer::~WebServer() {
    stop();
}

void WebServer::start(int port, bool async) {
    if (running_) return;
    running_ = true;

    if (async) {
        server_thread_ = std::thread([this, port]() {
            app_.port(port)
                .concurrency(2)
                .run();
        });
        LOG_INFO(log::get(), "Web server started on http://localhost:{}", port);
    } else {
        LOG_INFO(log::get(), "Web server starting on http://localhost:{}", port);
        app_.port(port)
            .concurrency(2)
            .run();
    }
}

void WebServer::stop() {
    if (!running_) return;
    running_ = false;
    // Stop streaming first — the stream thread may be holding the protocol
    // mutex or blocked on a screenshot, which would prevent Crow from shutting down.
    ws_handler_.stop_streaming();
    app_.stop();
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

} // namespace lvv
