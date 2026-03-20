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
// These globals are only accessed from the PocketPy thread (via py_bind callbacks)
// or set before scripts run (set_protocol, set_defaults). Safe because:
//  1. PocketPy VM is single-threaded — all py_bind callbacks run on the ScriptEngine thread
//  2. set_protocol/set_defaults are called during init, before any scripts execute
//  3. There is exactly one ScriptEngine instance (enforced by App owning it)
// If multiple ScriptEngine instances are ever needed, move these into a struct
// hung off the PocketPy VM's user data pointer.
static Protocol* g_protocol = nullptr;
static nlohmann::json g_object_map;
static std::string g_ref_images_dir = "ref_images";
static double g_default_threshold = 0.1;

void lvv_module_set_protocol(Protocol* protocol) {
    g_protocol = protocol;
}

void lvv_module_reset_state() {
    g_object_map.clear();
}

void lvv_module_set_defaults(const std::string& ref_dir, double threshold) {
    g_ref_images_dir = ref_dir;
    g_default_threshold = threshold;
}

// Resolve a name through the object map. If the name is in the map,
// return the mapped selector. Otherwise return the name as-is.
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

// Helper: check cancellation and protocol connection
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

// --- Python bindings ---

static bool py_lvv_ping(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(0);
    if (!check_protocol()) return false;

    try {
        auto version = g_protocol->ping();
        py_newstr(py_retval(), version.c_str());
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_click(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    auto name = resolve_name(py_tostr(py_arg(0)));
    if (!check_protocol()) return false;

    try {
        bool ok = g_protocol->click(name);
        py_newbool(py_retval(), ok);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_click_at(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(2);
    int x = py_toint(py_arg(0));
    int y = py_toint(py_arg(1));
    if (!check_protocol()) return false;

    try {
        bool ok = g_protocol->click_at(x, y);
        py_newbool(py_retval(), ok);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_press(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(2);
    int x = py_toint(py_arg(0));
    int y = py_toint(py_arg(1));
    if (!check_protocol()) return false;

    try {
        bool ok = g_protocol->press(x, y);
        py_newbool(py_retval(), ok);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_release(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(0);
    if (!check_protocol()) return false;

    try {
        bool ok = g_protocol->release();
        py_newbool(py_retval(), ok);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_move_to(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(2);
    int x = py_toint(py_arg(0));
    int y = py_toint(py_arg(1));
    if (!check_protocol()) return false;

    try {
        bool ok = g_protocol->move_to(x, y);
        py_newbool(py_retval(), ok);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_swipe(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(5);
    int x1 = py_toint(py_arg(0));
    int y1 = py_toint(py_arg(1));
    int x2 = py_toint(py_arg(2));
    int y2 = py_toint(py_arg(3));
    int duration = py_toint(py_arg(4));
    if (!check_protocol()) return false;

    try {
        bool ok = g_protocol->swipe(x1, y1, x2, y2, duration);
        py_newbool(py_retval(), ok);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_type_text(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    const char* text = py_tostr(py_arg(0));
    if (!check_protocol()) return false;

    try {
        bool ok = g_protocol->type_text(text);
        py_newbool(py_retval(), ok);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_key(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    const char* key_code = py_tostr(py_arg(0));
    if (!check_protocol()) return false;

    try {
        bool ok = g_protocol->key(key_code);
        py_newbool(py_retval(), ok);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_screenshot(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    const char* path = py_tostr(py_arg(0));
    if (!check_protocol()) return false;

    try {
        auto img = g_protocol->screenshot();
        if (!img.valid()) {
            return py_exception(tp_RuntimeError, "Failed to capture screenshot");
        }
        bool ok = save_png(img, path);
        py_newbool(py_retval(), ok);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_screenshot_compare(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(2);
    const char* ref_path_arg = py_tostr(py_arg(0));
    double threshold = py_tofloat(py_arg(1));
    if (!check_protocol()) return false;

    // Use default threshold if caller passes 0 or negative
    if (threshold <= 0.0) threshold = g_default_threshold;

    // Resolve relative paths against the ref_images directory
    std::string ref_path = ref_path_arg;
    if (!ref_path.empty() && ref_path[0] != '/') {
        ref_path = g_ref_images_dir + "/" + ref_path;
    }

    try {
        // Take current screenshot
        auto actual = g_protocol->screenshot();
        if (!actual.valid()) {
            return py_exception(tp_RuntimeError, "Failed to capture screenshot");
        }

        // Ensure ref directory exists
        auto parent = std::filesystem::path(ref_path).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }

        // Load or create reference
        auto reference = load_png(ref_path);
        if (!reference.valid()) {
            // First run: create reference
            save_png(actual, ref_path);
            py_newbool(py_retval(), true);
            return true;
        }

        CompareOptions opts;
        opts.diff_threshold = threshold;
        auto diff = compare_images(reference, actual, opts);

        py_newbool(py_retval(), diff.passed);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

// screenshot_compare with ignore regions: screenshot_compare_ex(ref_path, threshold, ignore_json)
// ignore_json is a JSON array string: "[[x,y,w,h], [x,y,w,h], ...]"
static bool py_lvv_screenshot_compare_ex(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(3);
    const char* ref_path_arg = py_tostr(py_arg(0));
    double threshold = py_tofloat(py_arg(1));
    const char* ignore_json = py_tostr(py_arg(2));
    if (!check_protocol()) return false;

    if (threshold <= 0.0) threshold = g_default_threshold;

    std::string ref_path = ref_path_arg;
    if (!ref_path.empty() && ref_path[0] != '/') {
        ref_path = g_ref_images_dir + "/" + ref_path;
    }

    try {
        auto actual = g_protocol->screenshot();
        if (!actual.valid()) {
            return py_exception(tp_RuntimeError, "Failed to capture screenshot");
        }

        auto parent = std::filesystem::path(ref_path).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }

        auto reference = load_png(ref_path);
        if (!reference.valid()) {
            save_png(actual, ref_path);
            py_newbool(py_retval(), true);
            return true;
        }

        CompareOptions opts;
        opts.diff_threshold = threshold;

        // Parse ignore regions from JSON string
        auto regions = nlohmann::json::parse(ignore_json, nullptr, false);
        if (regions.is_array()) {
            for (const auto& r : regions) {
                if (r.is_array() && r.size() == 4) {
                    opts.ignore_regions.push_back({
                        r[0].get<int>(), r[1].get<int>(),
                        r[2].get<int>(), r[3].get<int>()
                    });
                }
            }
        }

        auto diff = compare_images(reference, actual, opts);
        py_newbool(py_retval(), diff.passed);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

// --- Log capture ---

static bool py_lvv_set_log_capture(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    bool enable = py_tobool(py_arg(0));
    if (!check_protocol()) return false;
    try {
        bool ok = g_protocol->set_log_capture(enable);
        py_newbool(py_retval(), ok);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_get_logs(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(0);
    if (!check_protocol()) return false;
    try {
        auto resp = g_protocol->get_logs();
        py_newstr(py_retval(), resp.dump().c_str());
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_clear_logs(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(0);
    if (!check_protocol()) return false;
    try {
        bool ok = g_protocol->clear_logs();
        py_newbool(py_retval(), ok);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

// --- Performance metrics ---

static bool py_lvv_get_metrics(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(0);
    if (!check_protocol()) return false;
    try {
        auto resp = g_protocol->get_metrics();
        py_newstr(py_retval(), resp.dump().c_str());
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_wait(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    int ms = py_toint(py_arg(0));

    // Sleep in small increments so cancellation is responsive
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (is_cancelled()) {
            return py_exception(tp_RuntimeError, "Script cancelled (timeout)");
        }
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        auto chunk = std::min(remaining, std::chrono::milliseconds(100));
        if (chunk.count() > 0) {
            std::this_thread::sleep_for(chunk);
        }
    }
    py_newnone(py_retval());
    return true;
}

static bool py_lvv_assert_visible(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    auto name = resolve_name(py_tostr(py_arg(0)));
    if (!check_protocol()) return false;

    try {
        auto widget = g_protocol->find(name);
        if (!widget || !widget->visible) {
            return py_exception(tp_AssertionError,
                "Widget '%s' is not visible", name);
        }
        py_newbool(py_retval(), true);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_assert_hidden(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    auto name = resolve_name(py_tostr(py_arg(0)));
    if (!check_protocol()) return false;

    try {
        auto widget = g_protocol->find(name);
        if (widget && widget->visible) {
            return py_exception(tp_AssertionError,
                "Widget '%s' is visible (expected hidden)", name);
        }
        py_newbool(py_retval(), true);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_assert_value(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(3);
    auto name = resolve_name(py_tostr(py_arg(0)));
    const char* prop = py_tostr(py_arg(1));
    const char* expected = py_tostr(py_arg(2));
    if (!check_protocol()) return false;

    try {
        auto props = g_protocol->get_props(name, prop);
        std::string actual_val;
        if (props.contains(prop)) {
            actual_val = props[prop].dump();
            // Remove quotes for string values
            if (actual_val.size() >= 2 && actual_val.front() == '"') {
                actual_val = actual_val.substr(1, actual_val.size() - 2);
            }
        } else {
            return py_exception(tp_RuntimeError,
                "Widget '%s' has no property '%s'",
                name.c_str(), prop);
        }
        if (actual_val != expected) {
            return py_exception(tp_AssertionError,
                "Widget '%s' property '%s': expected '%s', got '%s'",
                name.c_str(), prop, expected, actual_val.c_str());
        }
        py_newbool(py_retval(), true);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

// Helper: get a widget property value as a string.
// Returns empty string and sets err_out on failure.
static std::string get_prop_value(const std::string& name, const char* prop,
                                  std::string& err_out) {
    auto props = g_protocol->get_props(name, prop);
    if (!props.contains(prop)) {
        err_out = "Widget '" + name + "' has no property '" + prop + "'";
        return {};
    }
    std::string val = props[prop].dump();
    if (val.size() >= 2 && val.front() == '"') {
        val = val.substr(1, val.size() - 2);
    }
    return val;
}

static bool py_lvv_assert_range(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(4);
    auto name = resolve_name(py_tostr(py_arg(0)));
    const char* prop = py_tostr(py_arg(1));
    double min_val = py_tofloat(py_arg(2));
    double max_val = py_tofloat(py_arg(3));
    if (!check_protocol()) return false;

    try {
        std::string err;
        auto val_str = get_prop_value(name, prop, err);
        if (!err.empty()) return py_exception(tp_RuntimeError, "%s", err.c_str());

        double actual;
        try { actual = std::stod(val_str); }
        catch (...) {
            return py_exception(tp_AssertionError,
                "Widget '%s' property '%s': '%s' is not a number",
                name.c_str(), prop, val_str.c_str());
        }

        if (actual < min_val || actual > max_val) {
            return py_exception(tp_AssertionError,
                "Widget '%s' property '%s': %.4g not in range [%.4g, %.4g]",
                name.c_str(), prop, actual, min_val, max_val);
        }
        py_newbool(py_retval(), true);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_assert_match(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(3);
    auto name = resolve_name(py_tostr(py_arg(0)));
    const char* prop = py_tostr(py_arg(1));
    const char* pattern = py_tostr(py_arg(2));
    if (!check_protocol()) return false;

    try {
        std::string err;
        auto val_str = get_prop_value(name, prop, err);
        if (!err.empty()) return py_exception(tp_RuntimeError, "%s", err.c_str());

        std::regex re;
        try { re = std::regex(pattern); }
        catch (const std::regex_error& e) {
            return py_exception(tp_ValueError, "Invalid regex '%s': %s",
                pattern, e.what());
        }

        if (!std::regex_search(val_str, re)) {
            return py_exception(tp_AssertionError,
                "Widget '%s' property '%s': '%s' does not match /%s/",
                name.c_str(), prop, val_str.c_str(), pattern);
        }
        py_newbool(py_retval(), true);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_assert_true(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(2);
    auto name = resolve_name(py_tostr(py_arg(0)));
    const char* prop = py_tostr(py_arg(1));
    if (!check_protocol()) return false;

    try {
        std::string err;
        auto val_str = get_prop_value(name, prop, err);
        if (!err.empty()) return py_exception(tp_RuntimeError, "%s", err.c_str());

        if (val_str != "true" && val_str != "1") {
            return py_exception(tp_AssertionError,
                "Widget '%s' property '%s': expected true, got '%s'",
                name.c_str(), prop, val_str.c_str());
        }
        py_newbool(py_retval(), true);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_assert_false(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(2);
    auto name = resolve_name(py_tostr(py_arg(0)));
    const char* prop = py_tostr(py_arg(1));
    if (!check_protocol()) return false;

    try {
        std::string err;
        auto val_str = get_prop_value(name, prop, err);
        if (!err.empty()) return py_exception(tp_RuntimeError, "%s", err.c_str());

        if (val_str != "false" && val_str != "0") {
            return py_exception(tp_AssertionError,
                "Widget '%s' property '%s': expected false, got '%s'",
                name.c_str(), prop, val_str.c_str());
        }
        py_newbool(py_retval(), true);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_get_tree(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(0);
    if (!check_protocol()) return false;

    try {
        auto tree = g_protocol->get_tree_cached();
        // Return as string (JSON) — PocketPy doesn't have dict literal creation easily
        std::string json_str = tree.dump();
        py_newstr(py_retval(), json_str.c_str());
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_find(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    auto name = resolve_name(py_tostr(py_arg(0)));
    if (!check_protocol()) return false;

    try {
        auto widget = g_protocol->find(name);
        if (!widget) {
            py_newnone(py_retval());
        } else {
            // Return basic info as JSON string
            nlohmann::json j;
            j["name"] = widget->name;
            j["type"] = widget->type;
            j["x"] = widget->x;
            j["y"] = widget->y;
            j["width"] = widget->width;
            j["height"] = widget->height;
            j["visible"] = widget->visible;
            j["auto_path"] = widget->auto_path;
            j["text"] = widget->text;
            py_newstr(py_retval(), j.dump().c_str());
        }
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_get_props(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    auto name = resolve_name(py_tostr(py_arg(0)));
    if (!check_protocol()) return false;

    try {
        auto props = g_protocol->get_props(name);
        py_newstr(py_retval(), props.dump().c_str());
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_screen_info(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(0);
    if (!check_protocol()) return false;

    try {
        auto info = g_protocol->get_screen_info();
        nlohmann::json j;
        j["width"] = info.width;
        j["height"] = info.height;
        j["color_format"] = info.color_format;
        py_newstr(py_retval(), j.dump().c_str());
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_find_at(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(2);
    int x = py_toint(py_arg(0));
    int y = py_toint(py_arg(1));
    if (!check_protocol()) return false;

    try {
        auto tree_json = g_protocol->get_tree_cached();
        WidgetTree tree;
        tree.update(tree_json);

        auto widget = tree.find_at(x, y);
        if (!widget) {
            py_newnone(py_retval());
        } else {
            nlohmann::json j;
            j["name"] = widget->name;
            j["type"] = widget->type;
            j["auto_path"] = widget->auto_path;
            j["text"] = widget->text;
            j["x"] = widget->x;
            j["y"] = widget->y;
            j["width"] = widget->width;
            j["height"] = widget->height;
            j["visible"] = widget->visible;
            j["clickable"] = widget->clickable;
            py_newstr(py_retval(), j.dump().c_str());
        }
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_widget_coords(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    auto name = resolve_name(py_tostr(py_arg(0)));
    if (!check_protocol()) return false;

    try {
        auto widget = g_protocol->find(name);
        if (!widget) {
            return py_exception(tp_RuntimeError, "Widget '%s' not found", name);
        }
        py_newtuple(py_retval(), 4);
        py_TValue* data = py_tuple_data(py_retval());
        py_newint(&data[0], widget->x);
        py_newint(&data[1], widget->y);
        py_newint(&data[2], widget->width);
        py_newint(&data[3], widget->height);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_get_all_widgets(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(0);
    if (!check_protocol()) return false;

    try {
        auto tree_json = g_protocol->get_tree_cached();
        WidgetTree tree;
        tree.update(tree_json);

        auto widgets = tree.flatten();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto* w : widgets) {
            nlohmann::json j;
            j["name"] = w->name;
            j["type"] = w->type;
            j["auto_path"] = w->auto_path;
            j["text"] = w->text;
            j["x"] = w->x;
            j["y"] = w->y;
            j["width"] = w->width;
            j["height"] = w->height;
            j["visible"] = w->visible;
            j["clickable"] = w->clickable;
            arr.push_back(j);
        }
        py_newstr(py_retval(), arr.dump().c_str());
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

// --- Multi-property selectors ---

static bool py_lvv_find_by(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    const char* expr = py_tostr(py_arg(0));
    if (!check_protocol()) return false;

    try {
        auto sel = parse_selector(expr);
        auto err = validate_selector(sel);
        if (!err.empty())
            return py_exception(tp_ValueError, "Invalid selector: %s", err.c_str());

        auto tree_json = g_protocol->get_tree_cached();
        WidgetTree tree;
        tree.update(tree_json);

        auto widget = tree.find_by_selector(sel);
        if (!widget) {
            py_newnone(py_retval());
        } else {
            nlohmann::json j;
            j["name"] = widget->name;
            j["type"] = widget->type;
            j["x"] = widget->x;
            j["y"] = widget->y;
            j["width"] = widget->width;
            j["height"] = widget->height;
            j["visible"] = widget->visible;
            j["auto_path"] = widget->auto_path;
            j["text"] = widget->text;
            py_newstr(py_retval(), j.dump().c_str());
        }
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_find_all_by(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    const char* expr = py_tostr(py_arg(0));
    if (!check_protocol()) return false;

    try {
        auto sel = parse_selector(expr);
        auto err = validate_selector(sel);
        if (!err.empty())
            return py_exception(tp_ValueError, "Invalid selector: %s", err.c_str());

        auto tree_json = g_protocol->get_tree_cached();
        WidgetTree tree;
        tree.update(tree_json);

        auto widgets = tree.find_all_by_selector(sel);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& w : widgets) {
            nlohmann::json j;
            j["name"] = w.name;
            j["type"] = w.type;
            j["x"] = w.x;
            j["y"] = w.y;
            j["width"] = w.width;
            j["height"] = w.height;
            j["visible"] = w.visible;
            j["auto_path"] = w.auto_path;
            j["text"] = w.text;
            arr.push_back(j);
        }
        py_newstr(py_retval(), arr.dump().c_str());
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

// --- Compound gestures ---

static bool py_lvv_long_press(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(3);
    int x = py_toint(py_arg(0));
    int y = py_toint(py_arg(1));
    int duration = py_toint(py_arg(2));
    if (!check_protocol()) return false;

    try {
        bool ok = g_protocol->long_press(x, y, duration, is_cancelled);
        py_newbool(py_retval(), ok);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

static bool py_lvv_drag(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(5);
    int x1 = py_toint(py_arg(0));
    int y1 = py_toint(py_arg(1));
    int x2 = py_toint(py_arg(2));
    int y2 = py_toint(py_arg(3));
    int duration = py_toint(py_arg(4));
    if (!check_protocol()) return false;

    try {
        bool ok = g_protocol->drag(x1, y1, x2, y2, duration, 10, is_cancelled);
        py_newbool(py_retval(), ok);
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "%s", e.what());
    }
}

// --- Retry-aware find ---

static bool py_lvv_find_with_retry(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(2);
    const char* name_or_selector = py_tostr(py_arg(0));
    int timeout_ms = py_toint(py_arg(1));
    if (!check_protocol()) return false;

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);
    constexpr int poll_interval_ms = 100;

    // Detect if this is a multi-property selector (contains '=')
    std::string expr(name_or_selector);
    bool is_selector = expr.find('=') != std::string::npos;

    // Validate selector once upfront
    WidgetSelector sel;
    if (is_selector) {
        sel = parse_selector(expr);
        auto err = validate_selector(sel);
        if (!err.empty())
            return py_exception(tp_ValueError, "Invalid selector: %s", err.c_str());
    }

    while (true) {
        if (is_cancelled()) {
            return py_exception(tp_RuntimeError, "Script cancelled (timeout)");
        }

        try {
            if (is_selector) {
                auto tree_json = g_protocol->get_tree_cached();
                WidgetTree tree;
                tree.update(tree_json);
                auto widget = tree.find_by_selector(sel);
                if (widget) {
                    nlohmann::json j;
                    j["name"] = widget->name;
                    j["type"] = widget->type;
                    j["x"] = widget->x;
                    j["y"] = widget->y;
                    j["width"] = widget->width;
                    j["height"] = widget->height;
                    j["visible"] = widget->visible;
                    j["auto_path"] = widget->auto_path;
                    j["text"] = widget->text;
                    py_newstr(py_retval(), j.dump().c_str());
                    return true;
                }
            } else {
                auto name = resolve_name(name_or_selector);
                auto widget = g_protocol->find(name);
                if (widget) {
                    nlohmann::json j;
                    j["name"] = widget->name;
                    j["type"] = widget->type;
                    j["x"] = widget->x;
                    j["y"] = widget->y;
                    j["width"] = widget->width;
                    j["height"] = widget->height;
                    j["visible"] = widget->visible;
                    j["auto_path"] = widget->auto_path;
                    j["text"] = widget->text;
                    py_newstr(py_retval(), j.dump().c_str());
                    return true;
                }
            }
        } catch (...) {}

        if (std::chrono::steady_clock::now() >= deadline) {
            return py_exception(tp_TimeoutError,
                "Timed out waiting for '%s' (%dms)",
                name_or_selector, timeout_ms);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
    }
}

// --- Object Map ---

static bool py_lvv_load_object_map(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(1);
    const char* path = py_tostr(py_arg(0));

    try {
        std::ifstream f(path);
        if (!f.is_open()) {
            return py_exception(tp_RuntimeError, "Cannot open object map: %s", path);
        }
        g_object_map = nlohmann::json::parse(f);
        py_newint(py_retval(), static_cast<int>(g_object_map.size()));
        return true;
    } catch (const std::exception& e) {
        return py_exception(tp_RuntimeError, "Invalid object map: %s", e.what());
    }
}

// --- wait_for / wait_until ---

// Helper: find a visible widget by pre-parsed selector
static std::optional<WidgetInfo> find_visible_by_selector(const WidgetSelector& sel) {
    auto tree_json = g_protocol->get_tree_cached();
    WidgetTree tree;
    tree.update(tree_json);
    auto widget = tree.find_by_selector(sel);
    if (widget && widget->visible) return widget;
    return std::nullopt;
}

// Helper: find a visible widget by name
static std::optional<WidgetInfo> find_visible_by_name(const std::string& name) {
    auto widget = g_protocol->find(name);
    if (widget && widget->visible) return widget;
    return std::nullopt;
}

static bool py_lvv_wait_for(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(2);
    const char* name_or_selector = py_tostr(py_arg(0));
    int timeout_ms = py_toint(py_arg(1));
    if (!check_protocol()) return false;

    std::string expr(name_or_selector);
    bool is_selector = expr.find('=') != std::string::npos;

    // Validate selector once upfront
    WidgetSelector sel;
    if (is_selector) {
        sel = parse_selector(expr);
        auto err = validate_selector(sel);
        if (!err.empty())
            return py_exception(tp_ValueError, "Invalid selector: %s", err.c_str());
    }

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);
    constexpr int poll_interval_ms = 100;

    while (true) {
        if (is_cancelled()) {
            return py_exception(tp_RuntimeError, "Script cancelled (timeout)");
        }

        try {
            auto found = is_selector
                ? find_visible_by_selector(sel)
                : find_visible_by_name(resolve_name(name_or_selector));
            if (found) {
                py_newbool(py_retval(), true);
                return true;
            }
        } catch (...) {}

        if (std::chrono::steady_clock::now() >= deadline) {
            return py_exception(tp_TimeoutError,
                "Timed out waiting for widget '%s' (%dms)",
                name_or_selector, timeout_ms);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
    }
}

static bool py_lvv_wait_until(int argc, py_StackRef argv) {
    PY_CHECK_ARGC(4);
    auto name = resolve_name(py_tostr(py_arg(0)));
    const char* prop = py_tostr(py_arg(1));
    const char* expected = py_tostr(py_arg(2));
    int timeout_ms = py_toint(py_arg(3));
    if (!check_protocol()) return false;

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);
    constexpr int poll_interval_ms = 100;
    std::string last_value;

    while (true) {
        if (is_cancelled()) {
            return py_exception(tp_RuntimeError, "Script cancelled (timeout)");
        }

        try {
            auto widget = g_protocol->find(name);
            if (widget) {
                // Check the "text" property directly from find result
                if (std::string(prop) == "text") {
                    if (widget->text == expected) {
                        py_newbool(py_retval(), true);
                        return true;
                    }
                    last_value = widget->text;
                } else {
                    auto props = g_protocol->get_props(name, prop);
                    if (props.contains(prop)) {
                        std::string val = props[prop].dump();
                        if (val.size() >= 2 && val.front() == '"') {
                            val = val.substr(1, val.size() - 2);
                        }
                        if (val == expected) {
                            py_newbool(py_retval(), true);
                            return true;
                        }
                        last_value = val;
                    }
                }
            }
        } catch (...) {}

        if (std::chrono::steady_clock::now() >= deadline) {
            return py_exception(tp_TimeoutError,
                "Timed out waiting for '%s'.%s == '%s' (last: '%s', %dms)",
                name.c_str(), prop, expected, last_value.c_str(), timeout_ms);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
    }
}

void lvv_module_register() {
    py_GlobalRef mod = py_newmodule("lvv");

    py_bind(mod, "ping()", py_lvv_ping);
    py_bind(mod, "click(name)", py_lvv_click);
    py_bind(mod, "click_at(x, y)", py_lvv_click_at);
    py_bind(mod, "press(x, y)", py_lvv_press);
    py_bind(mod, "release()", py_lvv_release);
    py_bind(mod, "move_to(x, y)", py_lvv_move_to);
    py_bind(mod, "swipe(x1, y1, x2, y2, duration)", py_lvv_swipe);
    py_bind(mod, "type_text(text)", py_lvv_type_text);
    py_bind(mod, "key(code)", py_lvv_key);
    py_bind(mod, "screenshot(path)", py_lvv_screenshot);
    py_bind(mod, "screenshot_compare(ref_path, threshold)", py_lvv_screenshot_compare);
    py_bind(mod, "wait(ms)", py_lvv_wait);
    py_bind(mod, "assert_visible(name)", py_lvv_assert_visible);
    py_bind(mod, "assert_hidden(name)", py_lvv_assert_hidden);
    py_bind(mod, "assert_value(name, prop, expected)", py_lvv_assert_value);
    py_bind(mod, "assert_range(name, prop, min, max)", py_lvv_assert_range);
    py_bind(mod, "assert_match(name, prop, pattern)", py_lvv_assert_match);
    py_bind(mod, "assert_true(name, prop)", py_lvv_assert_true);
    py_bind(mod, "assert_false(name, prop)", py_lvv_assert_false);
    py_bind(mod, "get_tree()", py_lvv_get_tree);
    py_bind(mod, "find(name)", py_lvv_find);
    py_bind(mod, "find_at(x, y)", py_lvv_find_at);
    py_bind(mod, "widget_coords(name)", py_lvv_widget_coords);
    py_bind(mod, "get_all_widgets()", py_lvv_get_all_widgets);
    py_bind(mod, "get_props(name)", py_lvv_get_props);
    py_bind(mod, "screen_info()", py_lvv_screen_info);
    py_bind(mod, "load_object_map(path)", py_lvv_load_object_map);
    py_bind(mod, "wait_for(name, timeout)", py_lvv_wait_for);
    py_bind(mod, "wait_until(name, prop, value, timeout)", py_lvv_wait_until);

    // Multi-property selectors
    py_bind(mod, "find_by(selector)", py_lvv_find_by);
    py_bind(mod, "find_all_by(selector)", py_lvv_find_all_by);

    // Compound gestures
    py_bind(mod, "long_press(x, y, duration)", py_lvv_long_press);
    py_bind(mod, "drag(x1, y1, x2, y2, duration)", py_lvv_drag);

    // Retry-aware find
    py_bind(mod, "find_with_retry(selector, timeout)", py_lvv_find_with_retry);

    // Screenshot compare with ignore regions
    py_bind(mod, "screenshot_compare_ex(ref_path, threshold, ignore_json)",
            py_lvv_screenshot_compare_ex);

    // Log capture
    py_bind(mod, "set_log_capture(enable)", py_lvv_set_log_capture);
    py_bind(mod, "get_logs()", py_lvv_get_logs);
    py_bind(mod, "clear_logs()", py_lvv_clear_logs);

    // Performance metrics
    py_bind(mod, "get_metrics()", py_lvv_get_metrics);
}

} // namespace lvv
