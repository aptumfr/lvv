#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/widget_tree.hpp"

using namespace lvv;
using json = nlohmann::json;

static json make_tree() {
    return {
        {"tree", {
            {"id", 1}, {"name", "screen"}, {"type", "lv_obj"},
            {"x", 0}, {"y", 0}, {"width", 480}, {"height", 320},
            {"visible", true}, {"clickable", false},
            {"auto_path", "screen"}, {"text", ""},
            {"children", {
                {
                    {"id", 2}, {"name", "header"}, {"type", "lv_obj"},
                    {"x", 0}, {"y", 0}, {"width", 480}, {"height", 50},
                    {"visible", true}, {"clickable", false},
                    {"auto_path", "screen/header"}, {"text", ""},
                    {"children", {
                        {
                            {"id", 3}, {"name", "title"}, {"type", "lv_label"},
                            {"x", 10}, {"y", 10}, {"width", 100}, {"height", 30},
                            {"visible", true}, {"clickable", false},
                            {"auto_path", "screen/header/title"},
                            {"text", "Hello"}
                        }
                    }}
                },
                {
                    {"id", 4}, {"name", "btn_ok"}, {"type", "lv_btn"},
                    {"x", 200}, {"y", 200}, {"width", 80}, {"height", 40},
                    {"visible", true}, {"clickable", true},
                    {"auto_path", "screen/btn_ok"}, {"text", "OK"}
                },
                {
                    {"id", 5}, {"name", "hidden_panel"}, {"type", "lv_obj"},
                    {"x", 0}, {"y", 0}, {"width", 480}, {"height", 320},
                    {"visible", false}, {"clickable", false},
                    {"auto_path", "screen/hidden_panel"}, {"text", ""}
                }
            }}
        }}
    };
}

TEST_CASE("WidgetTree::update parses tree correctly") {
    WidgetTree tree;
    tree.update(make_tree());

    CHECK_FALSE(tree.empty());
    CHECK(tree.root().name == "screen");
    CHECK(tree.root().children.size() == 3);
}

TEST_CASE("WidgetTree::update handles tree without wrapper") {
    json j = {
        {"id", 1}, {"name", "root"}, {"type", "lv_obj"},
        {"x", 0}, {"y", 0}, {"width", 100}, {"height", 100},
        {"visible", true}, {"clickable", false}
    };
    WidgetTree tree;
    tree.update(j);
    CHECK(tree.root().name == "root");
}

TEST_CASE("find_by_name finds direct child") {
    WidgetTree tree;
    tree.update(make_tree());

    auto result = tree.find_by_name("btn_ok");
    REQUIRE(result.has_value());
    CHECK(result->id == 4);
    CHECK(result->type == "lv_btn");
    CHECK(result->text == "OK");
}

TEST_CASE("find_by_name finds nested widget") {
    WidgetTree tree;
    tree.update(make_tree());

    auto result = tree.find_by_name("title");
    REQUIRE(result.has_value());
    CHECK(result->id == 3);
    CHECK(result->text == "Hello");
}

TEST_CASE("find_by_name finds by auto_path") {
    WidgetTree tree;
    tree.update(make_tree());

    auto result = tree.find_by_name("screen/header/title");
    REQUIRE(result.has_value());
    CHECK(result->name == "title");
}

TEST_CASE("find_by_name returns nullopt for missing widget") {
    WidgetTree tree;
    tree.update(make_tree());

    CHECK_FALSE(tree.find_by_name("nonexistent").has_value());
}

TEST_CASE("find_at returns deepest matching widget") {
    WidgetTree tree;
    tree.update(make_tree());

    // Should find the title label, not the header
    auto result = tree.find_at(15, 15);
    REQUIRE(result.has_value());
    CHECK(result->name == "title");
}

TEST_CASE("find_at returns button when clicking inside it") {
    WidgetTree tree;
    tree.update(make_tree());

    auto result = tree.find_at(220, 210);
    REQUIRE(result.has_value());
    CHECK(result->name == "btn_ok");
}

TEST_CASE("find_at ignores hidden widgets") {
    WidgetTree tree;
    tree.update(make_tree());

    // hidden_panel covers the whole screen but is invisible
    // At (300, 300), only screen and hidden_panel overlap, but hidden is invisible
    auto result = tree.find_at(300, 300);
    REQUIRE(result.has_value());
    CHECK(result->name == "screen");
}

TEST_CASE("flatten returns all widgets") {
    WidgetTree tree;
    tree.update(make_tree());

    auto all = tree.flatten();
    CHECK(all.size() == 5);  // screen, header, title, btn_ok, hidden_panel
}

TEST_CASE("to_json round-trips through update") {
    WidgetTree tree1;
    tree1.update(make_tree());

    auto j = tree1.to_json();

    WidgetTree tree2;
    tree2.update(j);

    CHECK(tree2.root().name == tree1.root().name);
    CHECK(tree2.root().children.size() == tree1.root().children.size());
    CHECK(tree2.flatten().size() == tree1.flatten().size());
}
