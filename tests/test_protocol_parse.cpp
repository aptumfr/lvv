#include <doctest/doctest.h>

// We test parse_widget through WidgetTree since Protocol::parse_widget is private.
// These tests verify the JSON → WidgetInfo parsing that both share.
#include "core/widget_tree.hpp"

using namespace lvv;
using json = nlohmann::json;

TEST_CASE("parse minimal widget JSON") {
    json j = {
        {"id", 42},
        {"name", "btn"},
        {"type", "lv_btn"},
        {"x", 10}, {"y", 20}, {"width", 100}, {"height", 50},
        {"visible", true}, {"clickable", true},
        {"auto_path", "screen/btn"},
        {"text", "Click me"}
    };

    WidgetTree tree;
    tree.update(j);

    CHECK(tree.root().id == 42);
    CHECK(tree.root().name == "btn");
    CHECK(tree.root().type == "lv_btn");
    CHECK(tree.root().x == 10);
    CHECK(tree.root().y == 20);
    CHECK(tree.root().width == 100);
    CHECK(tree.root().height == 50);
    CHECK(tree.root().visible);
    CHECK(tree.root().clickable);
    CHECK(tree.root().auto_path == "screen/btn");
    CHECK(tree.root().text == "Click me");
}

TEST_CASE("parse widget with missing optional fields uses defaults") {
    json j = {
        {"type", "lv_obj"}
    };

    WidgetTree tree;
    tree.update(j);

    CHECK(tree.root().id == 0);
    CHECK(tree.root().name.empty());
    CHECK(tree.root().x == 0);
    CHECK(tree.root().visible == false);
    CHECK(tree.root().children.empty());
}

TEST_CASE("parse nested children") {
    json j = {
        {"id", 1}, {"name", "parent"}, {"type", "lv_obj"},
        {"x", 0}, {"y", 0}, {"width", 100}, {"height", 100},
        {"visible", true},
        {"children", {
            {
                {"id", 2}, {"name", "child1"}, {"type", "lv_label"},
                {"x", 5}, {"y", 5}, {"width", 50}, {"height", 20},
                {"visible", true}, {"text", "A"}
            },
            {
                {"id", 3}, {"name", "child2"}, {"type", "lv_btn"},
                {"x", 5}, {"y", 30}, {"width", 50}, {"height", 20},
                {"visible", true},
                {"children", {
                    {
                        {"id", 4}, {"name", "grandchild"}, {"type", "lv_label"},
                        {"x", 10}, {"y", 35}, {"width", 40}, {"height", 10},
                        {"visible", true}, {"text", "B"}
                    }
                }}
            }
        }}
    };

    WidgetTree tree;
    tree.update(j);

    CHECK(tree.root().children.size() == 2);
    CHECK(tree.root().children[0].name == "child1");
    CHECK(tree.root().children[1].name == "child2");
    CHECK(tree.root().children[1].children.size() == 1);
    CHECK(tree.root().children[1].children[0].name == "grandchild");
}

TEST_CASE("empty children array is fine") {
    json j = {
        {"id", 1}, {"name", "leaf"}, {"type", "lv_label"},
        {"x", 0}, {"y", 0}, {"width", 50}, {"height", 20},
        {"visible", true},
        {"children", json::array()}
    };

    WidgetTree tree;
    tree.update(j);

    CHECK(tree.root().children.empty());
}
