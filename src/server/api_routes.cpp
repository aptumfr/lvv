#include "api_routes.hpp"
#include "api_types.hpp"
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

// ============================================================
// Route context — shared state passed to all handlers
// ============================================================

struct RouteCtx {
    Protocol* protocol;
    WidgetTree* tree;
    WSHandler* ws;
    ScriptEngine* script_engine;
    TestRunner* test_runner;
    std::string ref_images_dir;
};

// ============================================================
// Response helpers
// ============================================================

// JSON 200 response with exception-to-500 wrapping
static crow::response json_ok(const nlohmann::json& j) {
    crow::response resp(j.dump());
    resp.set_header("Content-Type", "application/json");
    return resp;
}

static crow::response json_err(int code, const std::string& msg) {
    crow::response resp(code, nlohmann::json{{"error", msg}}.dump());
    resp.set_header("Content-Type", "application/json");
    return resp;
}

// Parse JSON body, return nullopt on failure
static std::optional<nlohmann::json> parse_body(const crow::request& req) {
    auto j = nlohmann::json::parse(req.body, nullptr, false);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

// ============================================================
// Route registration helper
//
// Provides get/post/get_with_req methods that wire standalone handler
// functions to Crow routes. Handles body parsing, exception wrapping,
// and ctx passing — so the registration block is just:
//
//   routes.post("/api/click",  h_click);
//   routes.get("/api/tree",    h_tree);
// ============================================================

// Wrap a GET handler: fn(RouteCtx*) -> response
template <auto Fn>
auto wrap_get(RouteCtx* ctx) {
    return [ctx]() -> crow::response {
        try { return Fn(ctx); }
        catch (const std::exception& e) { return json_err(500, e.what()); }
    };
}

// Wrap a POST handler: fn(RouteCtx*, json body) -> response
template <auto Fn>
auto wrap_post(RouteCtx* ctx) {
    return [ctx](const crow::request& req) -> crow::response {
        auto body = parse_body(req);
        if (!body) return json_err(400, "Invalid JSON");
        try { return Fn(ctx, *body); }
        catch (const std::exception& e) { return json_err(500, e.what()); }
    };
}

// Wrap a GET handler that needs the request object (query params)
template <auto Fn>
auto wrap_get_req(RouteCtx* ctx) {
    return [ctx](const crow::request& req) -> crow::response {
        try { return Fn(ctx, req); }
        catch (const std::exception& e) { return json_err(500, e.what()); }
    };
}

// ============================================================
// Handler functions — each is a standalone function
// ============================================================

// --- Connection ---

static crow::response h_health(RouteCtx* ctx) {
    return json_ok({{"status", "ok"},
                    {"connected", ctx->protocol->is_connected()},
                    {"streaming", ctx->ws->is_streaming()},
                    {"clients", ctx->ws->client_count()}});
}

static crow::response h_ping(RouteCtx* ctx) {
    return json_ok({{"version", ctx->protocol->ping()}});
}

static crow::response h_connect(RouteCtx* ctx, const nlohmann::json&) {
    return json_ok({{"connected", ctx->protocol->connect()}});
}

static crow::response h_disconnect(RouteCtx* ctx, const nlohmann::json&) {
    ctx->ws->stop_streaming();
    ctx->protocol->disconnect();
    return json_ok({{"disconnected", true}});
}

static crow::response h_screen_info(RouteCtx* ctx) {
    auto info = ctx->protocol->get_screen_info();
    return json_ok({{"width", info.width},
                    {"height", info.height},
                    {"color_format", info.color_format}});
}

// --- Input ---

static crow::response h_click(RouteCtx* ctx, const nlohmann::json& body) {
    try {
        if (auto n = NameRequest::parse(body))
            return json_ok({{"success", ctx->protocol->click(n->name)}, {"received", true}});
        if (auto c = CoordsRequest::parse(body))
            return json_ok({{"success", ctx->protocol->click_at(c->x, c->y)}});
        return json_err(400, "Need 'name' or 'x','y'");
    } catch (const click_not_received& e) {
        return json_ok({{"success", true}, {"received", false},
                        {"error", e.what()}});
    }
}

static crow::response h_press(RouteCtx* ctx, const nlohmann::json& body) {
    auto c = CoordsRequest::parse(body);
    if (!c) return json_err(400, "Need 'x' and 'y'");
    return json_ok({{"success", ctx->protocol->press(c->x, c->y)}});
}

static crow::response h_move(RouteCtx* ctx, const nlohmann::json& body) {
    auto c = CoordsRequest::parse(body);
    if (!c) return json_err(400, "Need 'x' and 'y'");
    return json_ok({{"success", ctx->protocol->move_to(c->x, c->y)}});
}

static crow::response h_release(RouteCtx* ctx, const nlohmann::json&) {
    return json_ok({{"success", ctx->protocol->release()}});
}

static crow::response h_sync(RouteCtx* ctx, const nlohmann::json&) {
    ctx->protocol->sync();
    return json_ok({{"done", true}});
}

static crow::response h_type(RouteCtx* ctx, const nlohmann::json& body) {
    auto t = TextRequest::parse(body);
    if (!t) return json_err(400, "Need 'text'");
    return json_ok({{"success", ctx->protocol->type_text(t->text)}});
}

static crow::response h_key(RouteCtx* ctx, const nlohmann::json& body) {
    auto k = KeyRequest::parse(body);
    if (!k) return json_err(400, "Need 'key'");
    return json_ok({{"success", ctx->protocol->key(k->key)}});
}

static crow::response h_swipe(RouteCtx* ctx, const nlohmann::json& body) {
    auto g = GestureRequest::parse(body);
    if (!g) return json_err(400, "Invalid gesture parameters");
    return json_ok({{"success", ctx->protocol->swipe(g->x, g->y, g->x_end, g->y_end, g->duration)}});
}

static crow::response h_long_press(RouteCtx* ctx, const nlohmann::json& body) {
    auto c = CoordsRequest::parse(body);
    if (!c) return json_err(400, "Need 'x' and 'y'");
    const int duration = body.value("duration", 500);
    return json_ok({{"success", ctx->protocol->long_press(c->x, c->y, duration)}});
}

static crow::response h_drag(RouteCtx* ctx, const nlohmann::json& body) {
    auto g = GestureRequest::parse(body);
    if (!g) return json_err(400, "Invalid gesture parameters");
    return json_ok({{"success", ctx->protocol->drag(g->x, g->y, g->x_end, g->y_end, g->duration, g->steps)}});
}

// --- Inspection ---

static crow::response h_tree(RouteCtx* ctx) {
    auto tree_json = ctx->protocol->get_tree();
    std::lock_guard lock(ctx->tree->mutex);
    ctx->tree->update(tree_json);
    return json_ok(ctx->tree->to_json());
}

static crow::response h_find(RouteCtx* ctx, const crow::request& req) {
    auto name = req.url_params.get("name");
    if (!name) return json_err(400, "Need 'name' parameter");
    auto widget = ctx->protocol->find(name);
    if (!widget) return json_ok({{"found", false}});
    return json_ok(WidgetJson::from(*widget).to_find_json());
}

static crow::response h_find_at(RouteCtx* ctx, const nlohmann::json& body) {
    auto c = CoordsRequest::parse(body);
    if (!c) return json_err(400, "Need 'x' and 'y'");
    auto tree_json = ctx->protocol->get_tree_cached();
    std::lock_guard lock(ctx->tree->mutex);
    ctx->tree->update(tree_json);
    auto widget = ctx->tree->find_at(c->x, c->y);
    if (!widget) return json_ok({{"found", false}});
    return json_ok(WidgetJson::from(*widget).to_find_json());
}

static crow::response h_find_by(RouteCtx* ctx, const nlohmann::json& body) {
    if (!body.contains("selector")) return json_err(400, "Need 'selector'");
    auto sel = parse_selector(body["selector"].get<std::string>());
    auto sel_err = validate_selector(sel);
    if (!sel_err.empty()) return json_err(400, "Invalid selector: " + sel_err);

    auto tree_json = ctx->protocol->get_tree_cached();
    std::lock_guard lock(ctx->tree->mutex);
    ctx->tree->update(tree_json);
    const bool find_all = body.value("all", false);

    if (find_all) {
        auto widgets = ctx->tree->find_all_by_selector(sel);
        auto arr = widgets_to_json(widgets);
        return json_ok({{"found", !arr.empty()}, {"count", arr.size()}, {"widgets", arr}});
    } else {
        auto widget = ctx->tree->find_by_selector(sel);
        if (!widget) return json_ok({{"found", false}});
        return json_ok(WidgetJson::from(*widget).to_find_json());
    }
}

static crow::response h_widgets(RouteCtx* ctx) {
    auto tree_json = ctx->protocol->get_tree_cached();
    std::lock_guard lock(ctx->tree->mutex);
    ctx->tree->update(tree_json);
    return json_ok(widgets_to_json(ctx->tree->flatten()));
}

// --- Screenshots & visual regression ---

static crow::response h_screenshot(RouteCtx* ctx) {
    auto img = ctx->protocol->screenshot();
    if (!img.valid()) return json_err(500, "Failed to capture screenshot");
    auto png_data = encode_png(img);
    crow::response resp;
    resp.code = 200;
    resp.set_header("Content-Type", "image/png");
    resp.body.assign(reinterpret_cast<const char*>(png_data.data()), png_data.size());
    return resp;
}

static crow::response h_visual_compare(RouteCtx* ctx, const nlohmann::json& body) {
    auto cr = CompareRequest::parse(body);
    if (!cr) return json_err(400, "Need 'reference' image path");

    auto ref_path = cr->reference;
    if (!ref_path.empty() && ref_path[0] != '/')
        ref_path = ctx->ref_images_dir + "/" + ref_path;

    // Sandbox: allow paths under cwd or configured ref_images dir
    auto canonical = std::filesystem::weakly_canonical(ref_path).string();
    auto cwd_prefix = std::filesystem::current_path().string() + "/";
    auto ref_prefix = std::filesystem::weakly_canonical(ctx->ref_images_dir).string() + "/";
    if (canonical.rfind(cwd_prefix, 0) != 0 && canonical.rfind(ref_prefix, 0) != 0)
        return json_err(403, "Reference path outside allowed directories");

    auto actual = ctx->protocol->screenshot();
    if (!actual.valid()) throw std::runtime_error("Failed to capture screenshot");

    auto reference = load_png(ref_path);
    if (!reference.valid()) {
        auto parent = std::filesystem::path(ref_path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
        if (!save_png(actual, ref_path))
            throw std::runtime_error("Failed to write reference image");
        return json_ok({{"first_run", true}, {"passed", true},
                        {"message", "Reference image created"}});
    }

    CompareOptions opts;
    opts.diff_threshold = cr->threshold;
    opts.color_threshold = cr->color_threshold;
    opts.ignore_regions = cr->ignore_regions;
    auto diff = compare_images(reference, actual, opts);

    return json_ok({{"passed", diff.passed}, {"identical", diff.identical},
                    {"diff_percentage", diff.diff_percentage},
                    {"diff_pixels", diff.diff_pixels},
                    {"total_pixels", diff.total_pixels}});
}

// --- Test execution ---

static crow::response h_test_run(RouteCtx* ctx, const nlohmann::json& body) {
    if (!ctx->script_engine || !ctx->test_runner)
        return json_err(503, "Script engine not available");

    if (body.contains("code")) {
        auto [success, output] = ctx->script_engine->run_string(
            body["code"].get<std::string>());
        return json_ok({{"success", success}, {"output", output}});
    }

    if (body.contains("files")) {
        std::vector<std::string> files;
        for (const auto& f : body["files"])
            files.push_back(f.get<std::string>());
        auto suite = ctx->test_runner->run_suite("api_run", files);

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
                {"output", t.output}});
        }
        return json_ok(j);
    }

    return json_err(400, "Need 'code' or 'files'");
}

// --- Log capture ---

static crow::response h_get_logs(RouteCtx* ctx) {
    return json_ok(ctx->protocol->get_logs());
}

static crow::response h_clear_logs(RouteCtx* ctx, const nlohmann::json&) {
    return json_ok({{"success", ctx->protocol->clear_logs()}});
}

static crow::response h_log_capture(RouteCtx* ctx, const nlohmann::json& body) {
    const bool enable = body.value("enable", true);
    return json_ok({{"success", ctx->protocol->set_log_capture(enable)}});
}

// --- Metrics ---

static crow::response h_metrics(RouteCtx* ctx) {
    return json_ok(ctx->protocol->get_metrics());
}

// ============================================================
// Route registration
// ============================================================

void register_api_routes(CrowApp& app,
                         Protocol* protocol,
                         WidgetTree* tree,
                         WSHandler* ws,
                         ScriptEngine* script_engine,
                         TestRunner* test_runner,
                         const std::string& ref_images_dir) {

    // RouteCtx is heap-allocated and intentionally leaked — it must outlive the Crow app.
    // Each call creates its own context, so multiple apps don't interfere.
    auto* ctx = new RouteCtx{protocol, tree, ws, script_engine, test_runner, ref_images_dir};

    // Connection
    CROW_ROUTE(app, "/api/health")                              (wrap_get<h_health>(ctx));
    CROW_ROUTE(app, "/api/ping")                                (wrap_get<h_ping>(ctx));
    CROW_ROUTE(app, "/api/connect").methods("POST"_method)      (wrap_post<h_connect>(ctx));
    CROW_ROUTE(app, "/api/disconnect").methods("POST"_method)   (wrap_post<h_disconnect>(ctx));
    CROW_ROUTE(app, "/api/screen-info")                         (wrap_get<h_screen_info>(ctx));

    // Input
    CROW_ROUTE(app, "/api/click").methods("POST"_method)        (wrap_post<h_click>(ctx));
    CROW_ROUTE(app, "/api/press").methods("POST"_method)        (wrap_post<h_press>(ctx));
    CROW_ROUTE(app, "/api/move").methods("POST"_method)         (wrap_post<h_move>(ctx));
    CROW_ROUTE(app, "/api/release").methods("POST"_method)      (wrap_post<h_release>(ctx));
    CROW_ROUTE(app, "/api/type").methods("POST"_method)         (wrap_post<h_type>(ctx));
    CROW_ROUTE(app, "/api/key").methods("POST"_method)          (wrap_post<h_key>(ctx));
    CROW_ROUTE(app, "/api/swipe").methods("POST"_method)        (wrap_post<h_swipe>(ctx));
    CROW_ROUTE(app, "/api/long-press").methods("POST"_method)   (wrap_post<h_long_press>(ctx));
    CROW_ROUTE(app, "/api/sync").methods("POST"_method)         (wrap_post<h_sync>(ctx));
    CROW_ROUTE(app, "/api/drag").methods("POST"_method)         (wrap_post<h_drag>(ctx));

    // Inspection
    CROW_ROUTE(app, "/api/tree")                                (wrap_get<h_tree>(ctx));
    CROW_ROUTE(app, "/api/find")                                (wrap_get_req<h_find>(ctx));
    CROW_ROUTE(app, "/api/find-at").methods("POST"_method)      (wrap_post<h_find_at>(ctx));
    CROW_ROUTE(app, "/api/find-by").methods("POST"_method)      (wrap_post<h_find_by>(ctx));
    CROW_ROUTE(app, "/api/widgets")                             (wrap_get<h_widgets>(ctx));
    CROW_ROUTE(app, "/api/widget/<string>")([ctx](const std::string& name) -> crow::response {
        try { return json_ok(ctx->protocol->get_props(name)); }
        catch (const std::exception& e) { return json_err(500, e.what()); }
    });

    // Screenshots & visual regression
    CROW_ROUTE(app, "/api/screenshot")                          (wrap_get<h_screenshot>(ctx));
    CROW_ROUTE(app, "/api/visual/compare").methods("POST"_method)(wrap_post<h_visual_compare>(ctx));

    // Test execution
    CROW_ROUTE(app, "/api/test/run").methods("POST"_method)     (wrap_post<h_test_run>(ctx));

    // Log capture
    CROW_ROUTE(app, "/api/logs")                                (wrap_get<h_get_logs>(ctx));
    CROW_ROUTE(app, "/api/logs/clear").methods("POST"_method)   (wrap_post<h_clear_logs>(ctx));
    CROW_ROUTE(app, "/api/logs/capture").methods("POST"_method) (wrap_post<h_log_capture>(ctx));

    // Metrics
    CROW_ROUTE(app, "/api/metrics")                             (wrap_get<h_metrics>(ctx));
}

} // namespace lvv
