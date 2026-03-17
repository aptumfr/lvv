#include <doctest/doctest.h>

#include "core/widget_tree.hpp"

using namespace lvv;
using json = nlohmann::json;

static json make_tree() {
    return {
        {"tree", {
            {"id", 1}, {"name", "screen"}, {"type", "obj"},
            {"x", 0}, {"y", 0}, {"width", 480}, {"height", 320},
            {"visible", true}, {"clickable", false},
            {"auto_path", "screen"}, {"text", ""},
            {"children", {
                {
                    {"id", 2}, {"name", "header"}, {"type", "obj"},
                    {"x", 0}, {"y", 0}, {"width", 480}, {"height", 50},
                    {"visible", true}, {"clickable", false},
                    {"auto_path", "screen/header"}, {"text", ""},
                    {"children", {
                        {
                            {"id", 3}, {"name", "title"}, {"type", "label"},
                            {"x", 10}, {"y", 10}, {"width", 100}, {"height", 30},
                            {"visible", true}, {"clickable", false},
                            {"auto_path", "screen/header/title"},
                            {"text", "Hello"}
                        }
                    }}
                },
                {
                    {"id", 4}, {"name", "btn_ok"}, {"type", "button"},
                    {"x", 200}, {"y", 200}, {"width", 80}, {"height", 40},
                    {"visible", true}, {"clickable", true},
                    {"auto_path", "screen/btn_ok"}, {"text", "OK"}
                },
                {
                    {"id", 5}, {"name", "btn_cancel"}, {"type", "button"},
                    {"x", 300}, {"y", 200}, {"width", 80}, {"height", 40},
                    {"visible", true}, {"clickable", true},
                    {"auto_path", "screen/btn_cancel"}, {"text", "Cancel"}
                },
                {
                    {"id", 6}, {"name", ""}, {"type", "button"},
                    {"x", 400}, {"y", 200}, {"width", 40}, {"height", 40},
                    {"visible", false}, {"clickable", true},
                    {"auto_path", "screen/lv_btn_2"}, {"text", "Hidden"}
                },
                {
                    {"id", 7}, {"name", "slider"}, {"type", "slider"},
                    {"x", 50}, {"y", 100}, {"width", 200}, {"height", 20},
                    {"visible", true}, {"clickable", true},
                    {"auto_path", "screen/slider"}, {"text", ""}
                }
            }}
        }}
    };
}

// --- parse_selector tests ---

TEST_CASE("parse_selector: single key=value") {
    auto sel = parse_selector("type=button");
    CHECK(sel.size() == 1);
    CHECK(sel["type"] == "button");
}

TEST_CASE("parse_selector: multiple key=value") {
    auto sel = parse_selector("type=button,text=OK,visible=true");
    CHECK(sel.size() == 3);
    CHECK(sel["type"] == "button");
    CHECK(sel["text"] == "OK");
    CHECK(sel["visible"] == "true");
}

TEST_CASE("parse_selector: whitespace trimming") {
    auto sel = parse_selector(" type = button , text = OK ");
    CHECK(sel.size() == 2);
    CHECK(sel["type"] == "button");
    CHECK(sel["text"] == "OK");
}

TEST_CASE("parse_selector: empty string") {
    auto sel = parse_selector("");
    CHECK(sel.empty());
}

TEST_CASE("parse_selector: no equals sign ignored") {
    auto sel = parse_selector("garbage,type=button");
    CHECK(sel.size() == 1);
    CHECK(sel["type"] == "button");
}

TEST_CASE("parse_selector: text value with comma") {
    auto sel = parse_selector("text=Hello, world,type=label");
    CHECK(sel.size() == 2);
    CHECK(sel["text"] == "Hello, world");
    CHECK(sel["type"] == "label");
}

TEST_CASE("parse_selector: text value with equals sign") {
    auto sel = parse_selector("text=A=B,type=label");
    CHECK(sel.size() == 2);
    CHECK(sel["text"] == "A=B");
    CHECK(sel["type"] == "label");
}

TEST_CASE("parse_selector: text with comma and equals") {
    auto sel = parse_selector("type=label,text=x=1, y=2");
    CHECK(sel.size() == 2);
    CHECK(sel["type"] == "label");
    CHECK(sel["text"] == "x=1, y=2");
}

TEST_CASE("parse_selector: text value at end with comma") {
    auto sel = parse_selector("type=label,text=one, two, three");
    CHECK(sel.size() == 2);
    CHECK(sel["type"] == "label");
    CHECK(sel["text"] == "one, two, three");
}

// --- validate_selector tests ---

TEST_CASE("validate_selector: valid selector passes") {
    auto sel = parse_selector("type=button,text=OK");
    CHECK(validate_selector(sel).empty());
}

TEST_CASE("validate_selector: empty selector rejected") {
    auto sel = parse_selector("");
    CHECK(validate_selector(sel) == "empty selector");
}

TEST_CASE("validate_selector: all-garbage rejected as empty") {
    auto sel = parse_selector("garbage,nonsense");
    CHECK(validate_selector(sel) == "empty selector");
}

TEST_CASE("validate_selector: unknown key rejected") {
    auto sel = parse_selector("id=3");
    auto err = validate_selector(sel);
    CHECK_FALSE(err.empty());
    CHECK(err.find("id") != std::string::npos);
}

TEST_CASE("validate_selector: typo key rejected") {
    auto sel = parse_selector("tex=OK");
    auto err = validate_selector(sel);
    CHECK_FALSE(err.empty());
    CHECK(err.find("tex") != std::string::npos);
}

TEST_CASE("validate_selector: unknown key not absorbed into previous value") {
    // "foo" is not a known key, so the comma before it is NOT a boundary.
    // "foo=bar" becomes part of the type value, which won't match any widget.
    auto sel = parse_selector("type=button,foo=bar");
    CHECK(sel.size() == 1);
    CHECK(sel["type"] == "button,foo=bar");
}

TEST_CASE("validate_selector: unknown key at start rejected") {
    auto sel = parse_selector("foo=bar,type=button");
    auto err = validate_selector(sel);
    CHECK_FALSE(err.empty());
    CHECK(err.find("foo") != std::string::npos);
}

TEST_CASE("validate_selector: all valid keys accepted") {
    auto sel = parse_selector("type=button,name=ok,text=OK,visible=true,clickable=true,auto_path=screen/ok");
    CHECK(validate_selector(sel).empty());
}

TEST_CASE("validate_selector: boolean true/false/1/0 accepted") {
    CHECK(validate_selector(parse_selector("visible=true")).empty());
    CHECK(validate_selector(parse_selector("visible=false")).empty());
    CHECK(validate_selector(parse_selector("clickable=1")).empty());
    CHECK(validate_selector(parse_selector("clickable=0")).empty());
}

TEST_CASE("validate_selector: boolean typo rejected") {
    auto err = validate_selector(parse_selector("visible=yes"));
    CHECK_FALSE(err.empty());
    CHECK(err.find("visible") != std::string::npos);
    CHECK(err.find("yes") != std::string::npos);
}

TEST_CASE("validate_selector: boolean nonsense rejected") {
    auto err = validate_selector(parse_selector("clickable=maybe"));
    CHECK_FALSE(err.empty());
    CHECK(err.find("clickable") != std::string::npos);
}

// --- find_by_selector tests ---

TEST_CASE("find_by_selector: by type") {
    WidgetTree tree;
    tree.update(make_tree());

    auto sel = parse_selector("type=button");
    auto result = tree.find_by_selector(sel);
    REQUIRE(result.has_value());
    CHECK(result->name == "btn_ok");  // First btn in DFS order
}

TEST_CASE("find_by_selector: by type and text") {
    WidgetTree tree;
    tree.update(make_tree());

    auto sel = parse_selector("type=button,text=Cancel");
    auto result = tree.find_by_selector(sel);
    REQUIRE(result.has_value());
    CHECK(result->name == "btn_cancel");
}

TEST_CASE("find_by_selector: by type and visibility") {
    WidgetTree tree;
    tree.update(make_tree());

    auto sel = parse_selector("type=button,visible=false");
    auto result = tree.find_by_selector(sel);
    REQUIRE(result.has_value());
    CHECK(result->text == "Hidden");
}

TEST_CASE("find_by_selector: by clickable") {
    WidgetTree tree;
    tree.update(make_tree());

    auto sel = parse_selector("type=slider,clickable=true");
    auto result = tree.find_by_selector(sel);
    REQUIRE(result.has_value());
    CHECK(result->name == "slider");
}

TEST_CASE("find_by_selector: no match returns nullopt") {
    WidgetTree tree;
    tree.update(make_tree());

    auto sel = parse_selector("type=dropdown");
    CHECK_FALSE(tree.find_by_selector(sel).has_value());
}

TEST_CASE("find_by_selector: unknown key never matches") {
    WidgetTree tree;
    tree.update(make_tree());

    // Even though "type=button" would match, "foo=bar" is unknown and forces no match
    WidgetSelector sel = {{"type", "button"}, {"foo", "bar"}};
    CHECK_FALSE(tree.find_by_selector(sel).has_value());
}

TEST_CASE("find_by_selector: by name") {
    WidgetTree tree;
    tree.update(make_tree());

    auto sel = parse_selector("name=title");
    auto result = tree.find_by_selector(sel);
    REQUIRE(result.has_value());
    CHECK(result->type == "label");
    CHECK(result->text == "Hello");
}

// --- find_all_by_selector tests ---

TEST_CASE("find_all_by_selector: all buttons") {
    WidgetTree tree;
    tree.update(make_tree());

    auto sel = parse_selector("type=button");
    auto results = tree.find_all_by_selector(sel);
    CHECK(results.size() == 3);  // btn_ok, btn_cancel, hidden btn
}

TEST_CASE("find_all_by_selector: visible buttons only") {
    WidgetTree tree;
    tree.update(make_tree());

    auto sel = parse_selector("type=button,visible=true");
    auto results = tree.find_all_by_selector(sel);
    CHECK(results.size() == 2);  // btn_ok, btn_cancel
}

TEST_CASE("find_all_by_selector: no match returns empty") {
    WidgetTree tree;
    tree.update(make_tree());

    auto sel = parse_selector("type=chart");
    auto results = tree.find_all_by_selector(sel);
    CHECK(results.empty());
}

TEST_CASE("find_all_by_selector: all visible widgets") {
    WidgetTree tree;
    tree.update(make_tree());

    auto sel = parse_selector("visible=true");
    auto results = tree.find_all_by_selector(sel);
    CHECK(results.size() == 6);  // screen, header, title, btn_ok, btn_cancel, slider
}
