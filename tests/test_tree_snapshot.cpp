#include <doctest/doctest.h>

#include "core/tree_snapshot.hpp"

using namespace lvv;
using json = nlohmann::json;

static WidgetInfo make_widget(const std::string& type, const std::string& name,
                               bool visible = true, bool clickable = false) {
    WidgetInfo w;
    w.type = type;
    w.name = name;
    w.visible = visible;
    w.clickable = clickable;
    w.x = 10; w.y = 20; w.width = 100; w.height = 50;  // should be stripped
    return w;
}

TEST_CASE("normalize_tree strips volatile fields") {
    auto w = make_widget("button", "btn_ok", true, true);
    w.text = "OK";
    w.auto_path = "screen/btn_ok";
    w.id = 12345;

    auto j = normalize_tree(w);
    CHECK(j["type"] == "button");
    CHECK(j["name"] == "btn_ok");
    CHECK(j["text"] == "OK");
    CHECK(j["visible"] == true);
    CHECK(j["clickable"] == true);
    CHECK_FALSE(j.contains("x"));
    CHECK_FALSE(j.contains("y"));
    CHECK_FALSE(j.contains("width"));
    CHECK_FALSE(j.contains("height"));
    CHECK_FALSE(j.contains("id"));
    CHECK_FALSE(j.contains("auto_path"));
}

TEST_CASE("normalize_tree preserves children") {
    auto root = make_widget("obj", "screen");
    root.children.push_back(make_widget("button", "btn_a"));
    root.children.push_back(make_widget("label", "lbl"));

    auto j = normalize_tree(root);
    REQUIRE(j.contains("children"));
    CHECK(j["children"].size() == 2);
    CHECK(j["children"][0]["name"] == "btn_a");
    CHECK(j["children"][1]["name"] == "lbl");
}

TEST_CASE("diff_trees returns empty for identical trees") {
    auto w = make_widget("obj", "screen");
    auto j = normalize_tree(w);
    CHECK(diff_trees(j, j).empty());
}

TEST_CASE("diff_trees detects property change") {
    auto j1 = normalize_tree(make_widget("obj", "screen", true));
    auto j2 = normalize_tree(make_widget("obj", "screen", false));
    auto diffs = diff_trees(j1, j2);
    REQUIRE(diffs.size() == 1);
    CHECK(diffs[0].find("visible") != std::string::npos);
    CHECK(diffs[0].find("true -> false") != std::string::npos);
}

TEST_CASE("diff_trees detects name change") {
    auto j1 = normalize_tree(make_widget("button", "btn_ok"));
    auto j2 = normalize_tree(make_widget("button", "btn_cancel"));
    auto diffs = diff_trees(j1, j2);
    CHECK(!diffs.empty());
    CHECK(diffs[0].find("name") != std::string::npos);
}

TEST_CASE("diff_trees detects missing child") {
    auto root1 = make_widget("obj", "screen");
    root1.children.push_back(make_widget("button", "a"));
    root1.children.push_back(make_widget("button", "b"));

    auto root2 = make_widget("obj", "screen");
    root2.children.push_back(make_widget("button", "a"));

    auto diffs = diff_trees(normalize_tree(root1), normalize_tree(root2));
    REQUIRE(!diffs.empty());
    bool found = false;
    for (const auto& d : diffs)
        if (d.find("missing child") != std::string::npos) found = true;
    CHECK(found);
}

TEST_CASE("diff_trees detects extra child") {
    auto root1 = make_widget("obj", "screen");
    root1.children.push_back(make_widget("button", "a"));

    auto root2 = make_widget("obj", "screen");
    root2.children.push_back(make_widget("button", "a"));
    root2.children.push_back(make_widget("button", "extra"));

    auto diffs = diff_trees(normalize_tree(root1), normalize_tree(root2));
    REQUIRE(!diffs.empty());
    bool found = false;
    for (const auto& d : diffs)
        if (d.find("extra child") != std::string::npos) found = true;
    CHECK(found);
}

TEST_CASE("diff_trees handles named sibling reorder") {
    auto root1 = make_widget("obj", "screen");
    root1.children.push_back(make_widget("button", "btn_a"));
    root1.children.push_back(make_widget("button", "btn_b"));

    auto root2 = make_widget("obj", "screen");
    root2.children.push_back(make_widget("button", "btn_b"));  // swapped
    root2.children.push_back(make_widget("button", "btn_a"));  // swapped

    auto diffs = diff_trees(normalize_tree(root1), normalize_tree(root2));
    // Named children should match by name, not index — no diffs
    CHECK(diffs.empty());
}

TEST_CASE("diff_trees handles named sibling reorder with property change") {
    auto root1 = make_widget("obj", "screen");
    root1.children.push_back(make_widget("button", "btn_a", true));
    root1.children.push_back(make_widget("button", "btn_b", true));

    auto root2 = make_widget("obj", "screen");
    root2.children.push_back(make_widget("button", "btn_b", false));  // swapped + hidden
    root2.children.push_back(make_widget("button", "btn_a", true));   // swapped

    auto diffs = diff_trees(normalize_tree(root1), normalize_tree(root2));
    // btn_a matches by name — no diff. btn_b matches by name — visible changed.
    REQUIRE(diffs.size() == 1);
    CHECK(diffs[0].find("btn_b") != std::string::npos);
    CHECK(diffs[0].find("visible") != std::string::npos);
}

TEST_CASE("diff_trees recursive child comparison") {
    auto root1 = make_widget("obj", "screen");
    auto child1 = make_widget("obj", "panel");
    child1.children.push_back(make_widget("label", "title"));
    root1.children.push_back(child1);

    auto root2 = make_widget("obj", "screen");
    auto child2 = make_widget("obj", "panel");
    auto changed_label = make_widget("label", "title");
    changed_label.text = "Changed!";
    child2.children.push_back(changed_label);
    root2.children.push_back(child2);

    auto diffs = diff_trees(normalize_tree(root1), normalize_tree(root2));
    REQUIRE(!diffs.empty());
    CHECK(diffs[0].find("text") != std::string::npos);
    CHECK(diffs[0].find("panel") != std::string::npos);
}
