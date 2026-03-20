#include <doctest/doctest.h>

#include "server/api_types.hpp"

using namespace lvv;
using json = nlohmann::json;

TEST_CASE("WidgetJson::from preserves all fields") {
    WidgetInfo w;
    w.name = "btn_ok";
    w.type = "button";
    w.auto_path = "screen/btn_ok";
    w.text = "OK";
    w.x = 10; w.y = 20; w.width = 80; w.height = 40;
    w.visible = true;
    w.clickable = true;

    auto wj = WidgetJson::from(w);
    CHECK(wj.name == "btn_ok");
    CHECK(wj.type == "button");
    CHECK(wj.auto_path == "screen/btn_ok");
    CHECK(wj.text == "OK");
    CHECK(wj.x == 10);
    CHECK(wj.y == 20);
    CHECK(wj.width == 80);
    CHECK(wj.height == 40);
    CHECK(wj.visible == true);
    CHECK(wj.clickable == true);
}

TEST_CASE("WidgetJson::to_json produces correct keys") {
    WidgetJson wj{"btn", "button", "screen/btn", "Go",
                   5, 10, 100, 50, true, true};
    auto j = wj.to_json();

    CHECK(j["name"] == "btn");
    CHECK(j["type"] == "button");
    CHECK(j["auto_path"] == "screen/btn");
    CHECK(j["text"] == "Go");
    CHECK(j["x"] == 5);
    CHECK(j["y"] == 10);
    CHECK(j["width"] == 100);
    CHECK(j["height"] == 50);
    CHECK(j["visible"] == true);
    CHECK(j["clickable"] == true);
    CHECK(j.size() == 10);
}

TEST_CASE("WidgetJson::selector prefers name") {
    WidgetJson wj;
    wj.name = "btn_ok";
    wj.auto_path = "screen/btn_ok";
    CHECK(wj.selector() == "btn_ok");
}

TEST_CASE("WidgetJson::selector falls back to auto_path") {
    WidgetJson wj;
    wj.auto_path = "screen/label[OK]";
    CHECK(wj.selector() == "screen/label[OK]");
}

TEST_CASE("WidgetJson::selector returns null when empty") {
    WidgetJson wj;
    CHECK(wj.selector().is_null());
}

TEST_CASE("WidgetJson::to_find_json includes found and selector") {
    WidgetJson wj;
    wj.name = "title";
    wj.type = "label";
    auto j = wj.to_find_json();
    CHECK(j["found"] == true);
    CHECK(j["selector"] == "title");
    CHECK(j.contains("name"));
    CHECK(j.contains("type"));
}

TEST_CASE("widgets_to_json from vector<WidgetInfo>") {
    std::vector<WidgetInfo> widgets(2);
    widgets[0].name = "a";
    widgets[0].type = "button";
    widgets[1].name = "b";
    widgets[1].type = "label";

    auto arr = widgets_to_json(widgets);
    CHECK(arr.is_array());
    CHECK(arr.size() == 2);
    CHECK(arr[0]["name"] == "a");
    CHECK(arr[1]["name"] == "b");
}

TEST_CASE("widgets_to_json from vector<const WidgetInfo*>") {
    WidgetInfo w1, w2;
    w1.name = "x";
    w2.name = "y";
    std::vector<const WidgetInfo*> ptrs = {&w1, &w2};

    auto arr = widgets_to_json(ptrs);
    CHECK(arr.size() == 2);
    CHECK(arr[0]["name"] == "x");
    CHECK(arr[1]["name"] == "y");
}

TEST_CASE("CoordsRequest::parse valid") {
    json j = {{"x", 100}, {"y", 200}};
    auto c = CoordsRequest::parse(j);
    REQUIRE(c.has_value());
    CHECK(c->x == 100);
    CHECK(c->y == 200);
}

TEST_CASE("CoordsRequest::parse missing fields") {
    CHECK_FALSE(CoordsRequest::parse({{"x", 10}}).has_value());
    CHECK_FALSE(CoordsRequest::parse({{"y", 10}}).has_value());
    CHECK_FALSE(CoordsRequest::parse(json::object()).has_value());
}

TEST_CASE("CoordsRequest::parse bad types returns nullopt") {
    CHECK_FALSE(CoordsRequest::parse({{"x", "bad"}, {"y", 10}}).has_value());
}

TEST_CASE("NameRequest::parse valid") {
    auto n = NameRequest::parse({{"name", "btn_ok"}});
    REQUIRE(n.has_value());
    CHECK(n->name == "btn_ok");
}

TEST_CASE("NameRequest::parse missing") {
    CHECK_FALSE(NameRequest::parse(json::object()).has_value());
}

TEST_CASE("NameRequest::parse bad type") {
    CHECK_FALSE(NameRequest::parse({{"name", 123}}).has_value());
}

TEST_CASE("TextRequest::parse valid") {
    auto t = TextRequest::parse({{"text", "hello"}});
    REQUIRE(t.has_value());
    CHECK(t->text == "hello");
}

TEST_CASE("KeyRequest::parse valid") {
    auto k = KeyRequest::parse({{"key", "Enter"}});
    REQUIRE(k.has_value());
    CHECK(k->key == "Enter");
}

TEST_CASE("GestureRequest::parse defaults") {
    auto g = GestureRequest::parse(json::object());
    REQUIRE(g.has_value());
    CHECK(g->x == 0);
    CHECK(g->duration == 300);
    CHECK(g->steps == 10);
}

TEST_CASE("GestureRequest::parse overrides") {
    json j = {{"x", 10}, {"y", 20}, {"x_end", 30}, {"y_end", 40},
              {"duration", 500}, {"steps", 5}};
    auto g = GestureRequest::parse(j);
    REQUIRE(g.has_value());
    CHECK(g->x == 10);
    CHECK(g->y == 20);
    CHECK(g->x_end == 30);
    CHECK(g->y_end == 40);
    CHECK(g->duration == 500);
    CHECK(g->steps == 5);
}

TEST_CASE("GestureRequest::parse bad types returns nullopt") {
    json j = {{"x", "abc"}, {"y", 10}};
    CHECK_FALSE(GestureRequest::parse(j).has_value());
}

TEST_CASE("CompareRequest::parse valid") {
    json j = {{"reference", "home.png"}, {"threshold", 0.5}};
    auto cr = CompareRequest::parse(j);
    REQUIRE(cr.has_value());
    CHECK(cr->reference == "home.png");
    CHECK(cr->threshold == 0.5);
    CHECK(cr->color_threshold == 10.0);
    CHECK(cr->ignore_regions.empty());
}

TEST_CASE("CompareRequest::parse with ignore regions") {
    json j = {{"reference", "x.png"},
              {"ignore_regions", {{{"x", 0}, {"y", 0}, {"width", 50}, {"height", 30}}}}};
    auto cr = CompareRequest::parse(j);
    REQUIRE(cr.has_value());
    REQUIRE(cr->ignore_regions.size() == 1);
    CHECK(cr->ignore_regions[0].x == 0);
    CHECK(cr->ignore_regions[0].width == 50);
}

TEST_CASE("CompareRequest::parse missing reference") {
    CHECK_FALSE(CompareRequest::parse({{"threshold", 0.1}}).has_value());
}
