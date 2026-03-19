#include "api_routes.hpp"
#include "ws_handler.hpp"
#include "protocol/protocol.hpp"
#include "core/widget_tree.hpp"
#include "core/screen_capture.hpp"
#include "core/visual_regression.hpp"
#include "core/test_runner.hpp"
#include "scripting/script_engine.hpp"

#include <json.hpp>
#include <filesystem>
#include <functional>

namespace lvv {

// Wraps a lambda returning JSON into a crow::response with error handling
static crow::response json_route(std::function<nlohmann::json()> fn) {
    try {
        crow::response resp(fn().dump());
        resp.set_header("Content-Type", "application/json");
        return resp;
    } catch (const std::exception& e) {
        nlohmann::json err;
        err["error"] = e.what();
        return crow::response(500, err.dump());
    }
}

// Parse JSON body, return 400 on failure
static std::optional<nlohmann::json> parse_body(const crow::request& req) {
    auto j = nlohmann::json::parse(req.body, nullptr, false);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

void register_api_routes(CrowApp& app,
                         Protocol* protocol,
                         WidgetTree* tree,
                         WSHandler* ws,
                         ScriptEngine* script_engine,
                         TestRunner* test_runner,
                         const std::string& ref_images_dir) {
    // Health check
    CROW_ROUTE(app, "/api/health")
    ([protocol, ws]() {
        return json_route([&]() -> nlohmann::json {
            return {{"status", "ok"},
                    {"connected", protocol->is_connected()},
                    {"streaming", ws->is_streaming()},
                    {"clients", ws->client_count()}};
        });
    });

    // Connect to target
    CROW_ROUTE(app, "/api/connect").methods("POST"_method)
    ([protocol](const crow::request& req) {
        auto body = parse_body(req);
        if (!body) return crow::response(400, "Invalid JSON");

        bool ok = protocol->connect();
        return json_route([&]() -> nlohmann::json {
            return {{"connected", ok}};
        });
    });

    // Disconnect
    CROW_ROUTE(app, "/api/disconnect").methods("POST"_method)
    ([protocol, ws]() {
        ws->stop_streaming();
        protocol->disconnect();
        return crow::response(200, R"({"disconnected":true})");
    });

    // Get widget tree
    CROW_ROUTE(app, "/api/tree")
    ([protocol, tree]() {
        return json_route([&]() -> nlohmann::json {
            auto tree_json = protocol->get_tree();
            std::lock_guard lock(tree->mutex);
            tree->update(tree_json);
            return tree->to_json();
        });
    });

    // Get screenshot as PNG (binary response — can't use json_route)
    CROW_ROUTE(app, "/api/screenshot")
    ([protocol]() {
        try {
            auto img = protocol->screenshot();
            if (!img.valid()) {
                return crow::response(500, "Failed to capture screenshot");
            }
            auto png_data = encode_png(img);
            crow::response resp;
            resp.code = 200;
            resp.set_header("Content-Type", "image/png");
            resp.body.assign(
                reinterpret_cast<const char*>(png_data.data()), png_data.size());
            return resp;
        } catch (const std::exception& e) {
            return crow::response(500, e.what());
        }
    });

    // Click widget
    CROW_ROUTE(app, "/api/click").methods("POST"_method)
    ([protocol](const crow::request& req) {
        auto body = parse_body(req);
        if (!body) return crow::response(400, "Invalid JSON");

        if (body->contains("name")) {
            bool ok = protocol->click((*body)["name"].get<std::string>());
            return json_route([&]() -> nlohmann::json { return {{"success", ok}}; });
        } else if (body->contains("x") && body->contains("y")) {
            bool ok = protocol->click_at((*body)["x"].get<int>(), (*body)["y"].get<int>());
            return json_route([&]() -> nlohmann::json { return {{"success", ok}}; });
        }
        return crow::response(400, R"({"error":"Need 'name' or 'x','y'"})");
    });

    // Pointer press
    CROW_ROUTE(app, "/api/press").methods("POST"_method)
    ([protocol](const crow::request& req) {
        auto body = parse_body(req);
        if (!body || !body->contains("x") || !body->contains("y"))
            return crow::response(400, R"({"error":"Need 'x' and 'y'"})");

        return json_route([&]() -> nlohmann::json {
            return {{"success", protocol->press((*body)["x"].get<int>(),
                                                (*body)["y"].get<int>())}};
        });
    });

    // Pointer move
    CROW_ROUTE(app, "/api/move").methods("POST"_method)
    ([protocol](const crow::request& req) {
        auto body = parse_body(req);
        if (!body || !body->contains("x") || !body->contains("y"))
            return crow::response(400, R"({"error":"Need 'x' and 'y'"})");

        return json_route([&]() -> nlohmann::json {
            return {{"success", protocol->move_to((*body)["x"].get<int>(),
                                                  (*body)["y"].get<int>())}};
        });
    });

    // Pointer release
    CROW_ROUTE(app, "/api/release").methods("POST"_method)
    ([protocol]() {
        return json_route([&]() -> nlohmann::json {
            return {{"success", protocol->release()}};
        });
    });

    // Type text
    CROW_ROUTE(app, "/api/type").methods("POST"_method)
    ([protocol](const crow::request& req) {
        auto body = parse_body(req);
        if (!body || !body->contains("text"))
            return crow::response(400, R"({"error":"Need 'text'"})");

        return json_route([&]() -> nlohmann::json {
            return {{"success", protocol->type_text((*body)["text"].get<std::string>())}};
        });
    });

    // Send key
    CROW_ROUTE(app, "/api/key").methods("POST"_method)
    ([protocol](const crow::request& req) {
        auto body = parse_body(req);
        if (!body || !body->contains("key"))
            return crow::response(400, R"({"error":"Need 'key'"})");

        return json_route([&]() -> nlohmann::json {
            return {{"success", protocol->key((*body)["key"].get<std::string>())}};
        });
    });

    // Swipe
    CROW_ROUTE(app, "/api/swipe").methods("POST"_method)
    ([protocol](const crow::request& req) {
        auto body = parse_body(req);
        if (!body) return crow::response(400, "Invalid JSON");

        return json_route([&]() -> nlohmann::json {
            return {{"success", protocol->swipe(
                body->value("x", 0), body->value("y", 0),
                body->value("x_end", 0), body->value("y_end", 0),
                body->value("duration", 300))}};
        });
    });

    // Get widget properties
    CROW_ROUTE(app, "/api/widget/<string>")
    ([protocol](const std::string& name) {
        return json_route([&]() -> nlohmann::json {
            return protocol->get_props(name);
        });
    });

    // Get screen info
    CROW_ROUTE(app, "/api/screen-info")
    ([protocol]() {
        return json_route([&]() -> nlohmann::json {
            auto info = protocol->get_screen_info();
            return {{"width", info.width},
                    {"height", info.height},
                    {"color_format", info.color_format}};
        });
    });

    // Run test(s)
    CROW_ROUTE(app, "/api/test/run").methods("POST"_method)
    ([script_engine, test_runner](const crow::request& req) {
        auto body = parse_body(req);
        if (!body) return crow::response(400, "Invalid JSON");
        if (!script_engine || !test_runner)
            return crow::response(503, R"({"error":"Script engine not available"})");

        if (body->contains("code")) {
            return json_route([&]() -> nlohmann::json {
                auto [success, output] = script_engine->run_string(
                    (*body)["code"].get<std::string>());
                return {{"success", success}, {"output", output}};
            });
        } else if (body->contains("files")) {
            return json_route([&]() -> nlohmann::json {
                std::vector<std::string> files;
                for (const auto& f : (*body)["files"]) {
                    files.push_back(f.get<std::string>());
                }
                auto suite = test_runner->run_suite("api_run", files);

                nlohmann::json j;
                j["passed"] = suite.all_passed();
                j["total"] = suite.tests.size();
                j["pass_count"] = suite.passed();
                j["fail_count"] = suite.failed();
                j["duration"] = suite.total_duration_seconds;
                j["tests"] = nlohmann::json::array();
                for (const auto& t : suite.tests) {
                    j["tests"].push_back({
                        {"name", t.name},
                        {"status", (t.status == TestStatus::Pass) ? "pass" : "fail"},
                        {"duration", t.duration_seconds},
                        {"message", t.message},
                        {"output", t.output}
                    });
                }
                return j;
            });
        }

        return crow::response(400, R"({"error":"Need 'code' or 'files'"})");
    });

    // Find widget at coordinates (for smart recording)
    CROW_ROUTE(app, "/api/find-at").methods("POST"_method)
    ([protocol, tree](const crow::request& req) {
        auto body = parse_body(req);
        if (!body || !body->contains("x") || !body->contains("y"))
            return crow::response(400, R"({"error":"Need 'x' and 'y'"})");

        return json_route([&]() -> nlohmann::json {
            int x = (*body)["x"].get<int>();
            int y = (*body)["y"].get<int>();

            auto tree_json = protocol->get_tree_cached();
            std::lock_guard lock(tree->mutex);
            tree->update(tree_json);

            auto widget = tree->find_at(x, y);
            if (!widget) return {{"found", false}};

            nlohmann::json j;
            j["found"] = true;
            j["name"] = widget->name;
            j["type"] = widget->type;
            j["auto_path"] = widget->auto_path;
            j["text"] = widget->text;
            j["x"] = widget->x;
            j["y"] = widget->y;
            j["width"] = widget->width;
            j["height"] = widget->height;

            if (!widget->name.empty()) j["selector"] = widget->name;
            else if (!widget->auto_path.empty()) j["selector"] = widget->auto_path;
            else j["selector"] = nullptr;

            return j;
        });
    });

    // Visual diff comparison
    CROW_ROUTE(app, "/api/visual/compare").methods("POST"_method)
    ([protocol, ref_images_dir](const crow::request& req) {
        auto body = parse_body(req);
        if (!body || !body->contains("reference"))
            return crow::response(400, R"({"error":"Need 'reference' image path"})");

        // Resolve relative paths against ref_images dir
        auto ref_path = (*body)["reference"].get<std::string>();
        if (!ref_path.empty() && ref_path[0] != '/') {
            ref_path = ref_images_dir + "/" + ref_path;
        }
        // Sandbox: allow paths under cwd or the configured ref_images dir
        auto canonical = std::filesystem::weakly_canonical(ref_path).string();
        auto cwd_prefix = std::filesystem::current_path().string() + "/";
        auto ref_prefix = std::filesystem::weakly_canonical(ref_images_dir).string() + "/";
        if (canonical.rfind(cwd_prefix, 0) != 0 && canonical.rfind(ref_prefix, 0) != 0)
            return crow::response(403, R"({"error":"Reference path outside allowed directories"})");

        return json_route([&]() -> nlohmann::json {
            auto actual = protocol->screenshot();
            if (!actual.valid()) throw std::runtime_error("Failed to capture screenshot");

            auto reference = load_png(ref_path);
            if (!reference.valid()) {
                auto parent = std::filesystem::path(ref_path).parent_path();
                if (!parent.empty()) std::filesystem::create_directories(parent);
                if (!save_png(actual, ref_path))
                    throw std::runtime_error("Failed to write reference image");
                return {{"first_run", true}, {"passed", true},
                        {"message", "Reference image created"}};
            }

            CompareOptions opts;
            opts.diff_threshold = body->value("threshold", 0.1);
            opts.color_threshold = body->value("color_threshold", 10.0);
            if (body->contains("ignore_regions")) {
                for (const auto& r : (*body)["ignore_regions"]) {
                    opts.ignore_regions.push_back({
                        r.value("x", 0), r.value("y", 0),
                        r.value("width", 0), r.value("height", 0)
                    });
                }
            }
            auto diff = compare_images(reference, actual, opts);

            return {{"passed", diff.passed}, {"identical", diff.identical},
                    {"diff_percentage", diff.diff_percentage},
                    {"diff_pixels", diff.diff_pixels},
                    {"total_pixels", diff.total_pixels}};
        });
    });

    // Long press
    CROW_ROUTE(app, "/api/long-press").methods("POST"_method)
    ([protocol](const crow::request& req) {
        auto body = parse_body(req);
        if (!body || !body->contains("x") || !body->contains("y"))
            return crow::response(400, R"({"error":"Need 'x' and 'y'"})");

        return json_route([&]() -> nlohmann::json {
            return {{"success", protocol->long_press(
                (*body)["x"].get<int>(), (*body)["y"].get<int>(),
                body->value("duration", 500))}};
        });
    });

    // Drag
    CROW_ROUTE(app, "/api/drag").methods("POST"_method)
    ([protocol](const crow::request& req) {
        auto body = parse_body(req);
        if (!body) return crow::response(400, "Invalid JSON");

        return json_route([&]() -> nlohmann::json {
            return {{"success", protocol->drag(
                body->value("x", 0), body->value("y", 0),
                body->value("x_end", 0), body->value("y_end", 0),
                body->value("duration", 300),
                body->value("steps", 10))}};
        });
    });

    // Find by multi-property selector
    CROW_ROUTE(app, "/api/find-by").methods("POST"_method)
    ([protocol, tree](const crow::request& req) {
        auto body = parse_body(req);
        if (!body || !body->contains("selector"))
            return crow::response(400, R"({"error":"Need 'selector'"})");

        auto sel = parse_selector((*body)["selector"].get<std::string>());
        auto sel_err = validate_selector(sel);
        if (!sel_err.empty()) {
            nlohmann::json err = {{"error", "Invalid selector: " + sel_err}};
            return crow::response(400, err.dump());
        }

        return json_route([&, sel]() -> nlohmann::json {
            auto tree_json = protocol->get_tree_cached();
            std::lock_guard lock(tree->mutex);
            tree->update(tree_json);
            bool find_all = body->value("all", false);

            if (find_all) {
                auto widgets = tree->find_all_by_selector(sel);
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& w : widgets) {
                    arr.push_back({
                        {"name", w.name}, {"type", w.type},
                        {"auto_path", w.auto_path}, {"text", w.text},
                        {"x", w.x}, {"y", w.y},
                        {"width", w.width}, {"height", w.height},
                        {"visible", w.visible}, {"clickable", w.clickable}
                    });
                }
                return {{"found", !arr.empty()}, {"count", arr.size()}, {"widgets", arr}};
            } else {
                auto widget = tree->find_by_selector(sel);
                if (!widget) return nlohmann::json{{"found", false}};
                return {{"found", true},
                        {"name", widget->name}, {"type", widget->type},
                        {"auto_path", widget->auto_path}, {"text", widget->text},
                        {"x", widget->x}, {"y", widget->y},
                        {"width", widget->width}, {"height", widget->height},
                        {"visible", widget->visible}, {"clickable", widget->clickable}};
            }
        });
    });

    // Ping target
    CROW_ROUTE(app, "/api/ping")
    ([protocol]() {
        return json_route([&]() -> nlohmann::json {
            return {{"version", protocol->ping()}};
        });
    });

    // Find widget by name (for Python client)
    CROW_ROUTE(app, "/api/find")
    ([protocol](const crow::request& req) {
        auto name = req.url_params.get("name");
        if (!name)
            return crow::response(400, R"({"error":"Need 'name' parameter"})");
        return json_route([&, name]() -> nlohmann::json {
            auto widget = protocol->find(name);
            if (!widget) return {{"found", false}};
            return {{"found", true},
                    {"name", widget->name}, {"type", widget->type},
                    {"auto_path", widget->auto_path}, {"text", widget->text},
                    {"x", widget->x}, {"y", widget->y},
                    {"width", widget->width}, {"height", widget->height},
                    {"visible", widget->visible}, {"clickable", widget->clickable}};
        });
    });

    // All widgets flattened (for Python client)
    CROW_ROUTE(app, "/api/widgets")
    ([protocol, tree]() {
        return json_route([&]() -> nlohmann::json {
            auto tree_json = protocol->get_tree_cached();
            std::lock_guard lock(tree->mutex);
            tree->update(tree_json);
            auto widgets = tree->flatten();
            nlohmann::json arr = nlohmann::json::array();
            for (const auto* w : widgets) {
                arr.push_back({
                    {"name", w->name}, {"type", w->type},
                    {"auto_path", w->auto_path}, {"text", w->text},
                    {"x", w->x}, {"y", w->y},
                    {"width", w->width}, {"height", w->height},
                    {"visible", w->visible}, {"clickable", w->clickable}
                });
            }
            return arr;
        });
    });

    // --- Log capture ---

    CROW_ROUTE(app, "/api/logs")
    ([protocol]() {
        return json_route([&]() -> nlohmann::json {
            return protocol->get_logs();
        });
    });

    CROW_ROUTE(app, "/api/logs/clear").methods("POST"_method)
    ([protocol](const crow::request&) {
        return json_route([&]() -> nlohmann::json {
            return {{"success", protocol->clear_logs()}};
        });
    });

    CROW_ROUTE(app, "/api/logs/capture").methods("POST"_method)
    ([protocol](const crow::request& req) {
        auto body = parse_body(req);
        if (!body) return crow::response(400, R"({"error":"Invalid JSON"})");
        bool enable = body->value("enable", true);
        return json_route([&, enable]() -> nlohmann::json {
            return {{"success", protocol->set_log_capture(enable)}};
        });
    });

    // --- Performance metrics ---

    CROW_ROUTE(app, "/api/metrics")
    ([protocol]() {
        return json_route([&]() -> nlohmann::json {
            return protocol->get_metrics();
        });
    });
}

} // namespace lvv
