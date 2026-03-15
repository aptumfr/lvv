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

namespace lvv {

void register_api_routes(CrowApp& app,
                         Protocol* protocol,
                         WidgetTree* tree,
                         WSHandler* ws,
                         ScriptEngine* script_engine,
                         TestRunner* test_runner) {
    // Health check
    CROW_ROUTE(app, "/api/health")
    ([protocol, ws]() {
        nlohmann::json j;
        j["status"] = "ok";
        j["connected"] = protocol->is_connected();
        j["streaming"] = ws->is_streaming();
        j["clients"] = ws->client_count();

        crow::response resp(j.dump());
        resp.set_header("Content-Type", "application/json");
        return resp;
    });

    // Connect to target
    CROW_ROUTE(app, "/api/connect").methods("POST"_method)
    ([protocol](const crow::request& req) {
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) {
            return crow::response(400, "Invalid JSON");
        }

        // For now, TCP only
        // Host/port come from the app config, not per-request
        bool ok = protocol->connect();
        nlohmann::json j;
        j["connected"] = ok;

        crow::response resp(ok ? 200 : 500, j.dump());
        resp.set_header("Content-Type", "application/json");
        return resp;
    });

    // Disconnect
    CROW_ROUTE(app, "/api/disconnect").methods("POST"_method)
    ([protocol]() {
        protocol->disconnect();
        return crow::response(200, R"({"disconnected":true})");
    });

    // Get widget tree
    CROW_ROUTE(app, "/api/tree")
    ([protocol, tree]() {
        try {
            auto tree_json = protocol->get_tree();
            tree->update(tree_json);

            crow::response resp(tree->to_json().dump());
            resp.set_header("Content-Type", "application/json");
            return resp;
        } catch (const std::exception& e) {
            nlohmann::json err;
            err["error"] = e.what();
            return crow::response(500, err.dump());
        }
    });

    // Get screenshot as PNG
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
            resp.body = std::string(
                reinterpret_cast<const char*>(png_data.data()), png_data.size());
            return resp;
        } catch (const std::exception& e) {
            return crow::response(500, e.what());
        }
    });

    // Click widget
    CROW_ROUTE(app, "/api/click").methods("POST"_method)
    ([protocol](const crow::request& req) {
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) {
            return crow::response(400, "Invalid JSON");
        }

        try {
            bool ok;
            if (body.contains("name")) {
                ok = protocol->click(body["name"].get<std::string>());
            } else if (body.contains("x") && body.contains("y")) {
                ok = protocol->click_at(body["x"].get<int>(), body["y"].get<int>());
            } else {
                return crow::response(400, R"({"error":"Need 'name' or 'x','y'"})");
            }

            nlohmann::json j;
            j["success"] = ok;
            crow::response resp(j.dump());
            resp.set_header("Content-Type", "application/json");
            return resp;
        } catch (const std::exception& e) {
            nlohmann::json err;
            err["error"] = e.what();
            return crow::response(500, err.dump());
        }
    });

    // Pointer press
    CROW_ROUTE(app, "/api/press").methods("POST"_method)
    ([protocol](const crow::request& req) {
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("x") || !body.contains("y")) {
            return crow::response(400, R"({"error":"Need 'x' and 'y'"})");
        }

        try {
            bool ok = protocol->press(body["x"].get<int>(), body["y"].get<int>());
            nlohmann::json j;
            j["success"] = ok;
            crow::response resp(j.dump());
            resp.set_header("Content-Type", "application/json");
            return resp;
        } catch (const std::exception& e) {
            nlohmann::json err;
            err["error"] = e.what();
            return crow::response(500, err.dump());
        }
    });

    // Pointer move
    CROW_ROUTE(app, "/api/move").methods("POST"_method)
    ([protocol](const crow::request& req) {
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("x") || !body.contains("y")) {
            return crow::response(400, R"({"error":"Need 'x' and 'y'"})");
        }

        try {
            bool ok = protocol->move_to(body["x"].get<int>(), body["y"].get<int>());
            nlohmann::json j;
            j["success"] = ok;
            crow::response resp(j.dump());
            resp.set_header("Content-Type", "application/json");
            return resp;
        } catch (const std::exception& e) {
            nlohmann::json err;
            err["error"] = e.what();
            return crow::response(500, err.dump());
        }
    });

    // Pointer release
    CROW_ROUTE(app, "/api/release").methods("POST"_method)
    ([protocol]() {
        try {
            bool ok = protocol->release();
            nlohmann::json j;
            j["success"] = ok;
            crow::response resp(j.dump());
            resp.set_header("Content-Type", "application/json");
            return resp;
        } catch (const std::exception& e) {
            nlohmann::json err;
            err["error"] = e.what();
            return crow::response(500, err.dump());
        }
    });

    // Type text
    CROW_ROUTE(app, "/api/type").methods("POST"_method)
    ([protocol](const crow::request& req) {
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("text")) {
            return crow::response(400, R"({"error":"Need 'text'"})");
        }

        try {
            bool ok = protocol->type_text(body["text"].get<std::string>());
            nlohmann::json j;
            j["success"] = ok;
            crow::response resp(j.dump());
            resp.set_header("Content-Type", "application/json");
            return resp;
        } catch (const std::exception& e) {
            nlohmann::json err;
            err["error"] = e.what();
            return crow::response(500, err.dump());
        }
    });

    // Send key
    CROW_ROUTE(app, "/api/key").methods("POST"_method)
    ([protocol](const crow::request& req) {
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("key")) {
            return crow::response(400, R"({"error":"Need 'key'"})");
        }

        try {
            bool ok = protocol->key(body["key"].get<std::string>());
            nlohmann::json j;
            j["success"] = ok;
            crow::response resp(j.dump());
            resp.set_header("Content-Type", "application/json");
            return resp;
        } catch (const std::exception& e) {
            nlohmann::json err;
            err["error"] = e.what();
            return crow::response(500, err.dump());
        }
    });

    // Swipe
    CROW_ROUTE(app, "/api/swipe").methods("POST"_method)
    ([protocol](const crow::request& req) {
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) {
            return crow::response(400, "Invalid JSON");
        }

        try {
            bool ok = protocol->swipe(
                body.value("x", 0), body.value("y", 0),
                body.value("x_end", 0), body.value("y_end", 0),
                body.value("duration", 300));

            nlohmann::json j;
            j["success"] = ok;
            crow::response resp(j.dump());
            resp.set_header("Content-Type", "application/json");
            return resp;
        } catch (const std::exception& e) {
            nlohmann::json err;
            err["error"] = e.what();
            return crow::response(500, err.dump());
        }
    });

    // Get widget properties
    CROW_ROUTE(app, "/api/widget/<string>")
    ([protocol](const std::string& name) {
        try {
            auto props = protocol->get_props(name);
            crow::response resp(props.dump());
            resp.set_header("Content-Type", "application/json");
            return resp;
        } catch (const std::exception& e) {
            nlohmann::json err;
            err["error"] = e.what();
            return crow::response(500, err.dump());
        }
    });

    // Get screen info
    CROW_ROUTE(app, "/api/screen-info")
    ([protocol]() {
        try {
            auto info = protocol->get_screen_info();
            nlohmann::json j;
            j["width"] = info.width;
            j["height"] = info.height;
            j["color_format"] = info.color_format;

            crow::response resp(j.dump());
            resp.set_header("Content-Type", "application/json");
            return resp;
        } catch (const std::exception& e) {
            nlohmann::json err;
            err["error"] = e.what();
            return crow::response(500, err.dump());
        }
    });

    // Run test(s)
    CROW_ROUTE(app, "/api/test/run").methods("POST"_method)
    ([script_engine, test_runner](const crow::request& req) {
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) {
            return crow::response(400, "Invalid JSON");
        }

        try {
            if (body.contains("code")) {
                // Run inline code
                auto [success, output] = script_engine->run_string(
                    body["code"].get<std::string>());

                nlohmann::json j;
                j["success"] = success;
                j["output"] = output;
                crow::response resp(j.dump());
                resp.set_header("Content-Type", "application/json");
                return resp;
            } else if (body.contains("files")) {
                // Run test files
                std::vector<std::string> files;
                for (const auto& f : body["files"]) {
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
                    nlohmann::json tj;
                    tj["name"] = t.name;
                    tj["status"] = (t.status == TestStatus::Pass) ? "pass" : "fail";
                    tj["duration"] = t.duration_seconds;
                    tj["message"] = t.message;
                    tj["output"] = t.output;
                    j["tests"].push_back(tj);
                }

                crow::response resp(j.dump());
                resp.set_header("Content-Type", "application/json");
                return resp;
            }

            return crow::response(400, R"({"error":"Need 'code' or 'files'"})");
        } catch (const std::exception& e) {
            nlohmann::json err;
            err["error"] = e.what();
            return crow::response(500, err.dump());
        }
    });

    // Find widget at coordinates (for smart recording)
    CROW_ROUTE(app, "/api/find-at").methods("POST"_method)
    ([protocol, tree](const crow::request& req) {
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("x") || !body.contains("y")) {
            return crow::response(400, R"({"error":"Need 'x' and 'y'"})");
        }

        try {
            int x = body["x"].get<int>();
            int y = body["y"].get<int>();

            // First refresh the tree
            auto tree_json = protocol->get_tree();
            tree->update(tree_json);

            auto widget = tree->find_at(x, y);
            if (!widget) {
                nlohmann::json j;
                j["found"] = false;
                crow::response resp(j.dump());
                resp.set_header("Content-Type", "application/json");
                return resp;
            }

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

            // Best selector for recording
            if (!widget->name.empty()) {
                j["selector"] = widget->name;
            } else if (!widget->auto_path.empty()) {
                j["selector"] = widget->auto_path;
            } else {
                j["selector"] = nullptr;
            }

            crow::response resp(j.dump());
            resp.set_header("Content-Type", "application/json");
            return resp;
        } catch (const std::exception& e) {
            nlohmann::json err;
            err["error"] = e.what();
            return crow::response(500, err.dump());
        }
    });

    // Visual diff comparison
    CROW_ROUTE(app, "/api/visual/compare").methods("POST"_method)
    ([protocol](const crow::request& req) {
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("reference")) {
            return crow::response(400, R"({"error":"Need 'reference' image path"})");
        }

        try {
            // Take current screenshot
            auto actual = protocol->screenshot();
            if (!actual.valid()) {
                return crow::response(500, R"({"error":"Failed to capture screenshot"})");
            }

            // Sandbox reference path to current working directory
            auto ref_path = body["reference"].get<std::string>();
            auto base_dir = std::filesystem::current_path().string();
            auto canonical = std::filesystem::weakly_canonical(ref_path).string();
            if (canonical.find(base_dir) != 0) {
                return crow::response(403, R"({"error":"Reference path outside working directory"})");
            }

            auto reference = load_png(ref_path);

            if (!reference.valid()) {
                // First run: create reference
                save_png(actual, ref_path);
                nlohmann::json j;
                j["first_run"] = true;
                j["passed"] = true;
                j["message"] = "Reference image created";
                crow::response resp(j.dump());
                resp.set_header("Content-Type", "application/json");
                return resp;
            }

            CompareOptions opts;
            opts.diff_threshold = body.value("threshold", 0.1);
            opts.color_threshold = body.value("color_threshold", 10.0);

            auto diff = compare_images(reference, actual, opts);

            nlohmann::json j;
            j["passed"] = diff.passed;
            j["identical"] = diff.identical;
            j["diff_percentage"] = diff.diff_percentage;
            j["diff_pixels"] = diff.diff_pixels;
            j["total_pixels"] = diff.total_pixels;

            crow::response resp(j.dump());
            resp.set_header("Content-Type", "application/json");
            return resp;
        } catch (const std::exception& e) {
            nlohmann::json err;
            err["error"] = e.what();
            return crow::response(500, err.dump());
        }
    });

    // Ping target
    CROW_ROUTE(app, "/api/ping")
    ([protocol]() {
        try {
            auto version = protocol->ping();
            nlohmann::json j;
            j["version"] = version;
            crow::response resp(j.dump());
            resp.set_header("Content-Type", "application/json");
            return resp;
        } catch (const std::exception& e) {
            nlohmann::json err;
            err["error"] = e.what();
            return crow::response(500, err.dump());
        }
    });
}

} // namespace lvv
