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

// --- Subtree ---

TEST_CASE("find_subtree finds by name") {
    auto root = make_widget("obj", "screen");
    auto panel = make_widget("obj", "panel");
    panel.children.push_back(make_widget("button", "btn_ok"));
    root.children.push_back(panel);

    auto* found = find_subtree(root, "btn_ok");
    REQUIRE(found != nullptr);
    CHECK(found->name == "btn_ok");
}

TEST_CASE("find_subtree returns nullptr when not found") {
    auto root = make_widget("obj", "screen");
    CHECK(find_subtree(root, "nonexistent") == nullptr);
}

// --- Geometry ---

TEST_CASE("normalize_tree includes geometry when requested") {
    auto w = make_widget("obj", "test");
    w.x = 100; w.y = 200; w.width = 300; w.height = 400;

    auto without = normalize_tree(w);
    CHECK_FALSE(without.contains("x"));

    TreeSnapshotOptions opts;
    opts.include_geometry = true;
    auto with_geo = normalize_tree(w, opts);
    CHECK(with_geo["x"] == 100);
    CHECK(with_geo["y"] == 200);
    CHECK(with_geo["width"] == 300);
    CHECK(with_geo["height"] == 400);
}

TEST_CASE("diff_trees detects geometry change") {
    TreeSnapshotOptions opts;
    opts.include_geometry = true;

    auto w1 = make_widget("obj", "test");
    w1.x = 100; w1.y = 200;
    auto w2 = make_widget("obj", "test");
    w2.x = 150; w2.y = 200;

    auto diffs = diff_trees(normalize_tree(w1, opts), normalize_tree(w2, opts));
    REQUIRE(diffs.size() == 1);
    CHECK(diffs[0].find("x") != std::string::npos);
    CHECK(diffs[0].find("100") != std::string::npos);
    CHECK(diffs[0].find("150") != std::string::npos);
}

TEST_CASE("diff_trees respects geometry tolerance") {
    TreeSnapshotOptions opts;
    opts.include_geometry = true;

    auto w1 = make_widget("obj", "test");
    w1.x = 100; w1.y = 200; w1.width = 80; w1.height = 40;
    auto w2 = make_widget("obj", "test");
    w2.x = 103; w2.y = 198; w2.width = 80; w2.height = 40;

    // Without tolerance: 2 diffs (x and y)
    auto diffs_strict = diff_trees(normalize_tree(w1, opts), normalize_tree(w2, opts));
    CHECK(diffs_strict.size() == 2);

    // With tolerance of 5: no diffs (3 and 2 are within ±5)
    auto diffs_tolerant = diff_trees(normalize_tree(w1, opts), normalize_tree(w2, opts), "", 5);
    CHECK(diffs_tolerant.empty());
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
