#include "lvv_module.hpp"
#include "script_engine.hpp"
#include "protocol/protocol.hpp"
#include "core/screen_capture.hpp"
#include "core/visual_regression.hpp"
#include "core/widget_tree.hpp"

#include "pocketpy.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>
#include <thread>

namespace lvv {

// --- Module-level state ---
static Protocol* g_protocol = nullptr;
static nlohmann::json g_object_map;
static std::string g_ref_images_dir = "ref_images";
static double g_default_threshold = 0.1;

void lvv_module_set_protocol(Protocol* protocol) { g_protocol = protocol; }
void lvv_module_reset_state() { g_object_map.clear(); }
void lvv_module_set_defaults(const std::string& ref_dir, double threshold) {
    g_ref_images_dir = ref_dir;
    g_default_threshold = threshold;
}

static std::string resolve_name(const char* name) {
    if (!g_object_map.empty() && g_object_map.contains(name)) {
        return g_object_map[name].get<std::string>();
    }
    return name;
}

static bool is_cancelled() {
    auto* engine = ScriptEngine::active();
    return engine && engine->cancelled().load();
}

static bool check_protocol() {
    if (is_cancelled()) {
        py_exception(tp_RuntimeError, "Script cancelled (timeout)");
        return false;
    }
    if (!g_protocol || !g_protocol->is_connected()) {
        py_exception(tp_RuntimeError, "Not connected to target");
        return false;
    }
    return true;
}

// --- Binding wrapper ---
//
// Most lvv.* Python functions follow the same pattern:
//   1. Check argument count
//   2. Check protocol is connected
//   3. Do work (may throw)
//   4. Set py_retval()
//
// Instead of repeating this boilerplate 25+ times, we define each binding
// as a constexpr lambda (e.g. fn_click) and wrap it with py_protocol_cmd<fn_click>.
//
// The wrapper handles steps 1-2 and catches exceptions from step 3.
// The lambda only needs to do the actual work and set py_retval().
//
// Usage in registration:
//   py_bind(mod, "click(name)", py_protocol_cmd<fn_click>);
//
// The argument count is inferred from the signature string by PocketPy's
// py_bind(), so we don't need to specify it separately.

template <auto Fn>
static bool py_protocol_cmd(int argc, py_StackRef argv) {
    (void)argc;  // py_bind already validates argc from the signature string
    if (!check_protocol()) return false;
    try {
        Fn(argv);
        return true;
    } catch (const std::exception& e) {
        // All exceptions become RuntimeError. Functions that need finer-grained
        // exception types (ValueError, AssertionError, TimeoutError) should NOT
        // use py_protocol_cmd — write an explicit binding function instead.
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

// Convert a WidgetInfo to a JSON object.
// Contract: always contains these fields (Python scripts and web client depend on this):
//   name, type, x, y, width, height, visible, clickable, auto_path, text
static nlohmann::json widget_to_json(const WidgetInfo& w) {
    return {{"name", w.name}, {"type", w.type},
            {"x", w.x}, {"y", w.y},
            {"width", w.width}, {"height", w.height},
            {"visible", w.visible}, {"clickable", w.clickable},
            {"auto_path", w.auto_path}, {"text", w.text}};
}

// Set py_retval() to a widget JSON string, or None if empty.
static void ret_widget_json(const WidgetInfo& w) {
    py_newstr(py_retval(), widget_to_json(w).dump().c_str());
}

// Set py_retval() to a JSON array string of widgets.
static void ret_widget_array(const std::vector<WidgetInfo>& widgets) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& w : widgets) arr.push_back(widget_to_json(w));
    py_newstr(py_retval(), arr.dump().c_str());
}

static void ret_widget_array(const std::vector<const WidgetInfo*>& widgets) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto* w : widgets) arr.push_back(widget_to_json(*w));
    py_newstr(py_retval(), arr.dump().c_str());
}

static void ret_widget_or_none(const std::optional<WidgetInfo>& w) {
    if (w) ret_widget_json(*w);
    else py_newnone(py_retval());
}

// Get a widget property value as a normalized string.
// The spy returns properties as JSON values. This helper converts them
// to plain strings for comparison in assert_value/assert_range/assert_match/etc:
//   - JSON strings ("hello") → stripped quotes → hello
//   - JSON numbers (42) → "42"
//   - JSON booleans (true) → "true"
// This normalization must stay consistent across all assertion helpers.
static std::string get_prop_value(const std::string& name, const char* prop,
                                  std::string& err_out) {
    auto props = g_protocol->get_props(name, prop);
    if (!props.contains(prop)) {
        err_out = "Widget '" + name + "' has no property '" + prop + "'";
        return {};
    }
    std::string val = props[prop].dump();
    if (val.size() >= 2 && val.front() == '"')
        val = val.substr(1, val.size() - 2);
    return val;
}

// Helper: resolve ref image path
static std::string resolve_ref_path(const char* ref_path_arg) {
    std::string ref_path = ref_path_arg;
    if (!ref_path.empty() && ref_path[0] != '/')
        ref_path = g_ref_images_dir + "/" + ref_path;
    return ref_path;
}

// Helper: screenshot compare implementation
static bool do_screenshot_compare(const std::string& ref_path, CompareOptions& opts) {
    auto actual = g_protocol->screenshot();
    if (!actual.valid()) throw std::runtime_error("Failed to capture screenshot");

    auto parent = std::filesystem::path(ref_path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);

    auto reference = load_png(ref_path);
    if (!reference.valid()) {
        save_png(actual, ref_path);
        py_newbool(py_retval(), true);
        return true;
    }
    auto diff = compare_images(reference, actual, opts);
    py_newbool(py_retval(), diff.passed);
    return true;
}

// Helper: find visible by selector or name
static std::optional<WidgetInfo> find_visible_by_selector(const WidgetSelector& sel) {
    auto tree_json = g_protocol->get_tree_cached();
    WidgetTree tree;
    tree.update(tree_json);
    auto widget = tree.find_by_selector(sel);
    if (widget && widget->visible) return widget;
    return std::nullopt;
}

static std::optional<WidgetInfo> find_visible_by_name(const std::string& name) {
    auto widget = g_protocol->find(name);
    if (widget && widget->visible) return widget;
    return std::nullopt;
}

// ============================================================
// Simple bindings
// ============================================================
//
// Each lambda below implements one lvv.* Python function.
// It receives PocketPy's argv array and must set py_retval().
// Exceptions propagate to the py_protocol_cmd<> wrapper which converts
// them to Python RuntimeError.
//
// Use py_arg(0), py_arg(1), etc. to access arguments.
// Use py_tostr(), py_toint(), py_tofloat(), py_tobool() to convert.
// Use py_newbool(), py_newstr(), py_newnone() etc. to set the return value.

// -- Connection & info --

constexpr auto fn_ping = [](py_StackRef) {
    py_newstr(py_retval(), g_protocol->ping().c_str());
};
// -- Input --

constexpr auto fn_click = [](py_StackRef argv) {
    py_newbool(py_retval(), g_protocol->click(resolve_name(py_tostr(py_arg(0)))));
};
constexpr auto fn_click_at = [](py_StackRef argv) {
    py_newbool(py_retval(), g_protocol->click_at(py_toint(py_arg(0)), py_toint(py_arg(1))));
};
constexpr auto fn_press = [](py_StackRef argv) {
    py_newbool(py_retval(), g_protocol->press(py_toint(py_arg(0)), py_toint(py_arg(1))));
};
constexpr auto fn_release = [](py_StackRef) {
    py_newbool(py_retval(), g_protocol->release());
};
constexpr auto fn_move_to = [](py_StackRef argv) {
    py_newbool(py_retval(), g_protocol->move_to(py_toint(py_arg(0)), py_toint(py_arg(1))));
};
constexpr auto fn_swipe = [](py_StackRef argv) {
    const int x1 = py_toint(py_arg(0)), y1 = py_toint(py_arg(1));
    const int x2 = py_toint(py_arg(2)), y2 = py_toint(py_arg(3));
    const int duration = py_toint(py_arg(4));
    py_newbool(py_retval(), g_protocol->swipe(x1, y1, x2, y2, duration));
};
constexpr auto fn_type_text = [](py_StackRef argv) {
    py_newbool(py_retval(), g_protocol->type_text(py_tostr(py_arg(0))));
};
constexpr auto fn_key = [](py_StackRef argv) {
    py_newbool(py_retval(), g_protocol->key(py_tostr(py_arg(0))));
};
// -- Screenshots --

constexpr auto fn_screenshot = [](py_StackRef argv) {
    auto img = g_protocol->screenshot();
    if (!img.valid()) throw std::runtime_error("Failed to capture screenshot");
    py_newbool(py_retval(), save_png(img, py_tostr(py_arg(0))));
};
// -- Inspection --

constexpr auto fn_get_tree = [](py_StackRef) {
    py_newstr(py_retval(), g_protocol->get_tree().dump().c_str());
};
constexpr auto fn_get_props = [](py_StackRef argv) {
    py_newstr(py_retval(), g_protocol->get_props(resolve_name(py_tostr(py_arg(0)))).dump().c_str());
};
constexpr auto fn_screen_info = [](py_StackRef) {
    auto info = g_protocol->get_screen_info();
    nlohmann::json j = {{"width", info.width}, {"height", info.height},
                        {"color_format", info.color_format}};
    py_newstr(py_retval(), j.dump().c_str());
};
// -- Log capture & metrics --

constexpr auto fn_set_log_capture = [](py_StackRef argv) {
    py_newbool(py_retval(), g_protocol->set_log_capture(py_tobool(py_arg(0))));
};
constexpr auto fn_get_logs = [](py_StackRef) {
    py_newstr(py_retval(), g_protocol->get_logs().dump().c_str());
};
constexpr auto fn_clear_logs = [](py_StackRef) {
    py_newbool(py_retval(), g_protocol->clear_logs());
};
constexpr auto fn_get_metrics = [](py_StackRef) {
    py_newstr(py_retval(), g_protocol->get_metrics().dump().c_str());
};
constexpr auto fn_long_press = [](py_StackRef argv) {
    const int x = py_toint(py_arg(0)), y = py_toint(py_arg(1));
    const int duration = py_toint(py_arg(2));
    py_newbool(py_retval(), g_protocol->long_press(x, y, duration, is_cancelled));
};
constexpr auto fn_drag = [](py_StackRef argv) {
    const int x1 = py_toint(py_arg(0)), y1 = py_toint(py_arg(1));
    const int x2 = py_toint(py_arg(2)), y2 = py_toint(py_arg(3));
    const int duration = py_toint(py_arg(4));
    py_newbool(py_retval(), g_protocol->drag(x1, y1, x2, y2, duration, 10, is_cancelled));
};
constexpr auto fn_find = [](py_StackRef argv) {
    ret_widget_or_none(g_protocol->find(resolve_name(py_tostr(py_arg(0)))));
};
constexpr auto fn_find_at = [](py_StackRef argv) {
    auto tree_json = g_protocol->get_tree_cached();
    WidgetTree tree;
    tree.update(tree_json);
    ret_widget_or_none(tree.find_at(py_toint(py_arg(0)), py_toint(py_arg(1))));
};
constexpr auto fn_widget_coords = [](py_StackRef argv) {
    auto w = g_protocol->find(resolve_name(py_tostr(py_arg(0))));
    if (!w) throw std::runtime_error(std::string("Widget '") + py_tostr(py_arg(0)) + "' not found");
    py_newtuple(py_retval(), 4);
    py_TValue* d = py_tuple_data(py_retval());
    py_newint(&d[0], w->x); py_newint(&d[1], w->y);
    py_newint(&d[2], w->width); py_newint(&d[3], w->height);
};
constexpr auto fn_get_all_widgets = [](py_StackRef) {
    auto tree_json = g_protocol->get_tree_cached();
    WidgetTree tree;
    tree.update(tree_json);
    ret_widget_array(tree.flatten());
};
// find_by and find_all_by are explicit functions (not py_protocol_cmd) because
// they raise ValueError for invalid selectors, not RuntimeError.

static bool py_lvv_find_by(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    if (!check_protocol()) return false;
    auto sel = parse_selector(py_tostr(py_arg(0)));
    auto err = validate_selector(sel);
    if (!err.empty())
        return py_exception(tp_ValueError, "Invalid selector: %s", err.c_str());
    try {
        auto tree_json = g_protocol->get_tree_cached();
        WidgetTree tree;
        tree.update(tree_json);
        ret_widget_or_none(tree.find_by_selector(sel));
        return true;
    } catch (const std::exception& e) { return py_exception(tp_RuntimeError, "%s", e.what()); }
}

static bool py_lvv_find_all_by(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    if (!check_protocol()) return false;
    auto sel = parse_selector(py_tostr(py_arg(0)));
    auto err = validate_selector(sel);
    if (!err.empty())
        return py_exception(tp_ValueError, "Invalid selector: %s", err.c_str());
    try {
        auto tree_json = g_protocol->get_tree_cached();
        WidgetTree tree;
        tree.update(tree_json);
        ret_widget_array(tree.find_all_by_selector(sel));
        return true;
    } catch (const std::exception& e) { return py_exception(tp_RuntimeError, "%s", e.what()); }
}

// ============================================================
// Complex bindings (unique logic, kept as explicit functions)
// ============================================================

static bool py_lvv_screenshot_compare(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(2);
    if (!check_protocol()) return false;
    double threshold = py_tofloat(py_arg(1));
    if (threshold <= 0.0) threshold = g_default_threshold;
    auto ref_path = resolve_ref_path(py_tostr(py_arg(0)));
    try {
        CompareOptions opts;
        opts.diff_threshold = threshold;
        return do_screenshot_compare(ref_path, opts);
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_screenshot_compare_ex(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(3);
    if (!check_protocol()) return false;
    double threshold = py_tofloat(py_arg(1));
    if (threshold <= 0.0) threshold = g_default_threshold;
    auto ref_path = resolve_ref_path(py_tostr(py_arg(0)));
    try {
        CompareOptions opts;
        opts.diff_threshold = threshold;
        auto regions = nlohmann::json::parse(py_tostr(py_arg(2)), nullptr, false);
        if (regions.is_array()) {
            for (const auto& r : regions) {
                if (r.is_array() && r.size() == 4) {
                    opts.ignore_regions.push_back({
                        r[0].get<int>(), r[1].get<int>(),
                        r[2].get<int>(), r[3].get<int>()});
                }
            }
        }
        return do_screenshot_compare(ref_path, opts);
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_wait(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    const int ms = py_toint(py_arg(0));
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (is_cancelled())
            return py_exception(tp_RuntimeError, "Script cancelled (timeout)");
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        auto chunk = std::min(remaining, std::chrono::milliseconds(100));
        if (chunk.count() > 0) std::this_thread::sleep_for(chunk);
    }
    py_newnone(py_retval());
    return true;
}

static bool py_lvv_assert_visible(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    const auto name = resolve_name(py_tostr(py_arg(0)));
    if (!check_protocol()) return false;
    try {
        auto w = g_protocol->find(name);
        if (!w || !w->visible)
            return py_exception(tp_AssertionError, "Widget '%s' is not visible", name);
        py_newbool(py_retval(), true);
        return true;
    } catch (const std::exception& e) { return py_exception(tp_RuntimeError, "%s", e.what()); }
}

static bool py_lvv_assert_hidden(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    const auto name = resolve_name(py_tostr(py_arg(0)));
    if (!check_protocol()) return false;
    try {
        auto w = g_protocol->find(name);
        if (w && w->visible)
            return py_exception(tp_AssertionError, "Widget '%s' is visible (expected hidden)", name);
        py_newbool(py_retval(), true);
        return true;
    } catch (const std::exception& e) { return py_exception(tp_RuntimeError, "%s", e.what()); }
}

static bool py_lvv_assert_value(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(3);
    const auto name = resolve_name(py_tostr(py_arg(0)));
    const char* prop = py_tostr(py_arg(1));
    const char* expected = py_tostr(py_arg(2));
    if (!check_protocol()) return false;
    try {
        std::string err;
        auto val = get_prop_value(name, prop, err);
        if (!err.empty()) return py_exception(tp_RuntimeError, "%s", err.c_str());
        if (val != expected)
            return py_exception(tp_AssertionError,
                "Widget '%s' property '%s': expected '%s', got '%s'",
                name.c_str(), prop, expected, val.c_str());
        py_newbool(py_retval(), true);
        return true;
    } catch (const std::exception& e) { return py_exception(tp_RuntimeError, "%s", e.what()); }
}

static bool py_lvv_assert_range(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(4);
    const auto name = resolve_name(py_tostr(py_arg(0)));
    const char* prop = py_tostr(py_arg(1));
    const double min_val = py_tofloat(py_arg(2)), max_val = py_tofloat(py_arg(3));
    if (!check_protocol()) return false;
    try {
        std::string err;
        auto val_str = get_prop_value(name, prop, err);
        if (!err.empty()) return py_exception(tp_RuntimeError, "%s", err.c_str());
        double actual;
        try { actual = std::stod(val_str); }
        catch (...) {
            return py_exception(tp_AssertionError,
                "Widget '%s' property '%s': '%s' is not a number", name.c_str(), prop, val_str.c_str());
        }
        if (actual < min_val || actual > max_val)
            return py_exception(tp_AssertionError,
                "Widget '%s' property '%s': %.4g not in range [%.4g, %.4g]",
                name.c_str(), prop, actual, min_val, max_val);
        py_newbool(py_retval(), true);
        return true;
    } catch (const std::exception& e) { return py_exception(tp_RuntimeError, "%s", e.what()); }
}

static bool py_lvv_assert_match(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(3);
    const auto name = resolve_name(py_tostr(py_arg(0)));
    const char* prop = py_tostr(py_arg(1));
    const char* pattern = py_tostr(py_arg(2));
    if (!check_protocol()) return false;
    try {
        std::string err;
        auto val = get_prop_value(name, prop, err);
        if (!err.empty()) return py_exception(tp_RuntimeError, "%s", err.c_str());
        std::regex re;
        try { re = std::regex(pattern); }
        catch (const std::regex_error& e) {
            return py_exception(tp_ValueError, "Invalid regex '%s': %s", pattern, e.what());
        }
        if (!std::regex_search(val, re))
            return py_exception(tp_AssertionError,
                "Widget '%s' property '%s': '%s' does not match /%s/",
                name.c_str(), prop, val.c_str(), pattern);
        py_newbool(py_retval(), true);
        return true;
    } catch (const std::exception& e) { return py_exception(tp_RuntimeError, "%s", e.what()); }
}

static bool py_lvv_assert_true(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(2);
    const auto name = resolve_name(py_tostr(py_arg(0)));
    const char* prop = py_tostr(py_arg(1));
    if (!check_protocol()) return false;
    try {
        std::string err;
        auto val = get_prop_value(name, prop, err);
        if (!err.empty()) return py_exception(tp_RuntimeError, "%s", err.c_str());
        if (val != "true" && val != "1")
            return py_exception(tp_AssertionError,
                "Widget '%s' property '%s': expected true, got '%s'", name.c_str(), prop, val.c_str());
        py_newbool(py_retval(), true);
        return true;
    } catch (const std::exception& e) { return py_exception(tp_RuntimeError, "%s", e.what()); }
}

static bool py_lvv_assert_false(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(2);
    const auto name = resolve_name(py_tostr(py_arg(0)));
    const char* prop = py_tostr(py_arg(1));
    if (!check_protocol()) return false;
    try {
        std::string err;
        auto val = get_prop_value(name, prop, err);
        if (!err.empty()) return py_exception(tp_RuntimeError, "%s", err.c_str());
        if (val != "false" && val != "0")
            return py_exception(tp_AssertionError,
                "Widget '%s' property '%s': expected false, got '%s'", name.c_str(), prop, val.c_str());
        py_newbool(py_retval(), true);
        return true;
    } catch (const std::exception& e) { return py_exception(tp_RuntimeError, "%s", e.what()); }
}

static bool py_lvv_load_object_map(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    const char* path = py_tostr(py_arg(0));
    try {
        std::ifstream f(path);
        if (!f.is_open()) return py_exception(tp_RuntimeError, "Cannot open object map: %s", path);
        g_object_map = nlohmann::json::parse(f);
        py_newint(py_retval(), static_cast<int>(g_object_map.size()));
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "Invalid object map: %s", e.what());
    }
}

static bool py_lvv_find_with_retry(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(2);
    const char* name_or_selector = py_tostr(py_arg(0));
    const int timeout_ms = py_toint(py_arg(1));
    if (!check_protocol()) return false;

    std::string expr(name_or_selector);
    bool is_selector = expr.find('=') != std::string::npos;

    WidgetSelector sel;
    if (is_selector) {
        sel = parse_selector(expr);
        auto err = validate_selector(sel);
        if (!err.empty())
            return py_exception(tp_ValueError, "Invalid selector: %s", err.c_str());
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        if (is_cancelled())
            return py_exception(tp_RuntimeError, "Script cancelled (timeout)");
        try {
            if (is_selector) {
                auto tree_json = g_protocol->get_tree_cached();
                WidgetTree tree;
                tree.update(tree_json);
                auto widget = tree.find_by_selector(sel);
                if (widget) { ret_widget_json(*widget); return true; }
            } else {
                auto widget = g_protocol->find(resolve_name(name_or_selector));
                if (widget) { ret_widget_json(*widget); return true; }
            }
        } catch (...) {}
        if (std::chrono::steady_clock::now() >= deadline)
            return py_exception(tp_TimeoutError,
                "Timed out waiting for '%s' (%dms)", name_or_selector, timeout_ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

static bool py_lvv_wait_for(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(2);
    const char* name_or_selector = py_tostr(py_arg(0));
    const int timeout_ms = py_toint(py_arg(1));
    if (!check_protocol()) return false;

    std::string expr(name_or_selector);
    bool is_selector = expr.find('=') != std::string::npos;

    WidgetSelector sel;
    if (is_selector) {
        sel = parse_selector(expr);
        auto err = validate_selector(sel);
        if (!err.empty())
            return py_exception(tp_ValueError, "Invalid selector: %s", err.c_str());
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        if (is_cancelled())
            return py_exception(tp_RuntimeError, "Script cancelled (timeout)");
        try {
            auto found = is_selector
                ? find_visible_by_selector(sel)
                : find_visible_by_name(resolve_name(name_or_selector));
            if (found) { py_newbool(py_retval(), true); return true; }
        } catch (...) {}
        if (std::chrono::steady_clock::now() >= deadline)
            return py_exception(tp_TimeoutError,
                "Timed out waiting for widget '%s' (%dms)", name_or_selector, timeout_ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

static bool py_lvv_wait_until(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(4);
    const auto name = resolve_name(py_tostr(py_arg(0)));
    const char* prop = py_tostr(py_arg(1));
    const char* expected = py_tostr(py_arg(2));
    const int timeout_ms = py_toint(py_arg(3));
    if (!check_protocol()) return false;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::string last_value;
    while (true) {
        if (is_cancelled())
            return py_exception(tp_RuntimeError, "Script cancelled (timeout)");
        try {
            auto widget = g_protocol->find(name);
            if (widget) {
                if (std::string(prop) == "text") {
                    if (widget->text == expected) { py_newbool(py_retval(), true); return true; }
                    last_value = widget->text;
                } else {
                    auto props = g_protocol->get_props(name, prop);
                    if (props.contains(prop)) {
                        std::string val = props[prop].dump();
                        if (val.size() >= 2 && val.front() == '"')
                            val = val.substr(1, val.size() - 2);
                        if (val == expected) { py_newbool(py_retval(), true); return true; }
                        last_value = val;
                    }
                }
            }
        } catch (...) {}
        if (std::chrono::steady_clock::now() >= deadline)
            return py_exception(tp_TimeoutError,
                "Timed out waiting for '%s'.%s == '%s' (last: '%s', %dms)",
                name.c_str(), prop, expected, last_value.c_str(), timeout_ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// ============================================================
// Registration
// ============================================================

void lvv_module_register() {
    py_GlobalRef mod = py_newmodule("lvv");

    // --- Simple bindings (protocol check + exception handling via py_protocol_cmd wrapper) ---

    // Connection
    py_bind(mod, "ping()",                              py_protocol_cmd<fn_ping>);
    py_bind(mod, "screen_info()",                       py_protocol_cmd<fn_screen_info>);

    // Input
    py_bind(mod, "click(name)",                         py_protocol_cmd<fn_click>);
    py_bind(mod, "click_at(x, y)",                      py_protocol_cmd<fn_click_at>);
    py_bind(mod, "press(x, y)",                         py_protocol_cmd<fn_press>);
    py_bind(mod, "release()",                           py_protocol_cmd<fn_release>);
    py_bind(mod, "move_to(x, y)",                       py_protocol_cmd<fn_move_to>);
    py_bind(mod, "swipe(x1, y1, x2, y2, duration)",     py_protocol_cmd<fn_swipe>);
    py_bind(mod, "type_text(text)",                     py_protocol_cmd<fn_type_text>);
    py_bind(mod, "key(code)",                           py_protocol_cmd<fn_key>);
    py_bind(mod, "long_press(x, y, duration)",          py_protocol_cmd<fn_long_press>);
    py_bind(mod, "drag(x1, y1, x2, y2, duration)",      py_protocol_cmd<fn_drag>);

    // Inspection
    py_bind(mod, "find(name)",                          py_protocol_cmd<fn_find>);
    py_bind(mod, "find_at(x, y)",                       py_protocol_cmd<fn_find_at>);
    py_bind(mod, "find_by(selector)",                   py_lvv_find_by);
    py_bind(mod, "find_all_by(selector)",               py_lvv_find_all_by);
    py_bind(mod, "widget_coords(name)",                 py_protocol_cmd<fn_widget_coords>);
    py_bind(mod, "get_all_widgets()",                   py_protocol_cmd<fn_get_all_widgets>);
    py_bind(mod, "get_tree()",                          py_protocol_cmd<fn_get_tree>);
    py_bind(mod, "get_props(name)",                     py_protocol_cmd<fn_get_props>);

    // Screenshots
    py_bind(mod, "screenshot(path)",                    py_protocol_cmd<fn_screenshot>);

    // Log capture
    py_bind(mod, "set_log_capture(enable)",             py_protocol_cmd<fn_set_log_capture>);
    py_bind(mod, "get_logs()",                          py_protocol_cmd<fn_get_logs>);
    py_bind(mod, "clear_logs()",                        py_protocol_cmd<fn_clear_logs>);

    // Metrics
    py_bind(mod, "get_metrics()",                       py_protocol_cmd<fn_get_metrics>);

    // --- Complex bindings (unique control flow, kept as explicit functions) ---
    py_bind(mod, "screenshot_compare(ref_path, threshold)", py_lvv_screenshot_compare);
    py_bind(mod, "screenshot_compare_ex(ref_path, threshold, ignore_json)", py_lvv_screenshot_compare_ex);
    py_bind(mod, "wait(ms)",                            py_lvv_wait);
    py_bind(mod, "assert_visible(name)",                py_lvv_assert_visible);
    py_bind(mod, "assert_hidden(name)",                 py_lvv_assert_hidden);
    py_bind(mod, "assert_value(name, prop, expected)",  py_lvv_assert_value);
    py_bind(mod, "assert_range(name, prop, min, max)",  py_lvv_assert_range);
    py_bind(mod, "assert_match(name, prop, pattern)",   py_lvv_assert_match);
    py_bind(mod, "assert_true(name, prop)",             py_lvv_assert_true);
    py_bind(mod, "assert_false(name, prop)",            py_lvv_assert_false);
    py_bind(mod, "load_object_map(path)",               py_lvv_load_object_map);
    py_bind(mod, "find_with_retry(selector, timeout)",  py_lvv_find_with_retry);
    py_bind(mod, "wait_for(name, timeout)",             py_lvv_wait_for);
    py_bind(mod, "wait_until(name, prop, value, timeout)", py_lvv_wait_until);
}

} // namespace lvv
