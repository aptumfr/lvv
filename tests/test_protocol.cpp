#include <doctest/doctest.h>

#include "protocol/protocol.hpp"
#include "transport/transport.hpp"

#include <json.hpp>
#include <queue>

using namespace lvv;
using json = nlohmann::json;

// ============================================================
// Mock transport — queues canned responses for Protocol to consume
// ============================================================

class MockTransport : public ITransport {
public:
    bool connected = true;

    // Queue a JSON response line
    void queue(const json& j) { responses_.push(j.dump()); }

    // Queue a raw string response
    void queue_raw(const std::string& s) { responses_.push(s); }

    // Track what was sent
    std::vector<std::string> sent;

    bool connect() override { connected = true; return true; }
    void disconnect() override { connected = false; }
    bool is_connected() const override { return connected; }
    void abort() override {}

    bool send(std::string_view data) override {
        if (!connected) return false;
        sent.push_back(std::string(data));
        return true;
    }

    std::optional<std::string> receive(std::chrono::milliseconds) override {
        if (!connected || responses_.empty()) return std::nullopt;
        auto resp = responses_.front();
        responses_.pop();
        return resp;
    }

    std::optional<std::vector<uint8_t>> receive_bytes(
        size_t count, std::chrono::milliseconds) override {
        if (!connected) return std::nullopt;
        return std::vector<uint8_t>(count, 0);
    }

private:
    std::queue<std::string> responses_;
};

// ============================================================
// Protocol::ping
// ============================================================

TEST_CASE("Protocol::ping returns version") {
    MockTransport transport;
    Protocol proto(&transport);

    transport.queue({{"version", "1.2.3"}});
    CHECK(proto.ping() == "1.2.3");
}

TEST_CASE("Protocol::ping throws on disconnect") {
    MockTransport transport;
    transport.connected = false;
    Protocol proto(&transport);

    CHECK_THROWS_AS(proto.ping(), std::runtime_error);
}

// ============================================================
// Protocol::find
// ============================================================

TEST_CASE("Protocol::find returns widget") {
    MockTransport transport;
    Protocol proto(&transport);

    transport.queue({{"widget", {
        {"name", "btn_ok"}, {"type", "button"},
        {"x", 10}, {"y", 20}, {"width", 80}, {"height", 40},
        {"visible", true}, {"clickable", true},
        {"auto_path", "btn_ok"}, {"text", "OK"}
    }}});

    auto w = proto.find("btn_ok");
    REQUIRE(w.has_value());
    CHECK(w->name == "btn_ok");
    CHECK(w->type == "button");
    CHECK(w->x == 10);
    CHECK(w->visible == true);
}

TEST_CASE("Protocol::find returns nullopt for not found") {
    MockTransport transport;
    Protocol proto(&transport);

    transport.queue({{"error", "Widget not found"}});

    auto w = proto.find("nonexistent");
    CHECK_FALSE(w.has_value());
}

TEST_CASE("Protocol::find throws on transport error") {
    MockTransport transport;
    Protocol proto(&transport);
    // No response queued — receive() returns nullopt
    CHECK_THROWS_AS(proto.find("btn"), std::runtime_error);
}

// ============================================================
// Protocol::click / fire_and_forget
// ============================================================

TEST_CASE("Protocol::click returns true on success") {
    MockTransport transport;
    Protocol proto(&transport);

    transport.queue({{"success", true}});
    CHECK(proto.click("btn_ok") == true);
}

TEST_CASE("Protocol::click returns false for not found") {
    MockTransport transport;
    Protocol proto(&transport);

    transport.queue({{"error", "Widget not found"}});
    CHECK(proto.click("nonexistent") == false);
}

TEST_CASE("Protocol::click throws on transport error") {
    MockTransport transport;
    Protocol proto(&transport);
    // No response — transport error
    CHECK_THROWS_AS(proto.click("btn"), std::runtime_error);
}

TEST_CASE("Protocol::click throws click_not_received when intercepted") {
    MockTransport transport;
    Protocol proto(&transport);

    transport.queue({{"success", true}, {"received", false}});
    CHECK_THROWS_AS(proto.click("btn_ok"), click_not_received);
}

TEST_CASE("Protocol::click succeeds with received=true") {
    MockTransport transport;
    Protocol proto(&transport);

    transport.queue({{"success", true}, {"received", true}});
    CHECK(proto.click("btn_ok") == true);
}

// ============================================================
// Protocol::sync
// ============================================================

TEST_CASE("Protocol::sync sends command") {
    MockTransport transport;
    Protocol proto(&transport);

    transport.queue({{"done", true}});
    proto.sync();  // should not throw

    REQUIRE(!transport.sent.empty());
    auto cmd = json::parse(transport.sent[0]);
    CHECK(cmd["cmd"] == "sync");
}

TEST_CASE("Protocol::sync throws on transport error") {
    MockTransport transport;
    Protocol proto(&transport);
    // No response
    CHECK_THROWS_AS(proto.sync(), std::runtime_error);
}

TEST_CASE("Protocol::click_at sends coordinates") {
    MockTransport transport;
    Protocol proto(&transport);

    transport.queue({{"success", true}});
    CHECK(proto.click_at(100, 200) == true);

    REQUIRE(!transport.sent.empty());
    auto sent = json::parse(transport.sent[0]);
    CHECK(sent["cmd"] == "click_at");
    CHECK(sent["x"] == 100);
    CHECK(sent["y"] == 200);
}

// ============================================================
// Protocol::get_tree and cache
// ============================================================

TEST_CASE("Protocol::get_tree returns tree JSON") {
    MockTransport transport;
    Protocol proto(&transport);

    json tree = {{"tree", {{"type", "obj"}, {"name", "screen"}}}};
    transport.queue(tree);

    auto result = proto.get_tree();
    CHECK(result.contains("tree"));
    CHECK(result["tree"]["name"] == "screen");
}

TEST_CASE("Protocol::get_tree_cached avoids repeated fetches") {
    MockTransport transport;
    Protocol proto(&transport);

    json tree = {{"tree", {{"type", "obj"}, {"name", "screen"}}}};
    transport.queue(tree);

    // First call fetches
    auto r1 = proto.get_tree_cached(1000);
    CHECK(r1.contains("tree"));
    CHECK(transport.sent.size() == 1);

    // Second call within TTL returns cache — no new send
    auto r2 = proto.get_tree_cached(1000);
    CHECK(r2.contains("tree"));
    CHECK(transport.sent.size() == 1);  // still 1
}

TEST_CASE("Protocol::invalidate_tree_cache forces re-fetch") {
    MockTransport transport;
    Protocol proto(&transport);

    json tree = {{"tree", {{"type", "obj"}, {"name", "screen"}}}};
    transport.queue(tree);
    proto.get_tree_cached(1000);
    CHECK(transport.sent.size() == 1);

    proto.invalidate_tree_cache();

    transport.queue(tree);
    proto.get_tree_cached(1000);
    CHECK(transport.sent.size() == 2);  // forced re-fetch
}

// ============================================================
// Protocol error handling — widget_not_found vs runtime_error
// ============================================================

TEST_CASE("widget_not_found is distinct from runtime_error") {
    try {
        throw widget_not_found("test");
    } catch (const widget_not_found& e) {
        CHECK(std::string(e.what()) == "test");
    }
    // Also catchable as runtime_error
    try {
        throw widget_not_found("test2");
    } catch (const std::runtime_error& e) {
        CHECK(std::string(e.what()) == "test2");
    }
}

TEST_CASE("Protocol throws widget_not_found for target not-found errors") {
    MockTransport transport;
    Protocol proto(&transport);

    transport.queue({{"error", "Widget not found"}});
    // find() catches widget_not_found and returns nullopt
    CHECK_FALSE(proto.find("x").has_value());

    // But a different error propagates as runtime_error
    transport.queue({{"error", "Some other error"}});
    CHECK_THROWS_AS(proto.find("x"), std::runtime_error);
}

// ============================================================
// Protocol::get_props
// ============================================================

TEST_CASE("Protocol::get_props returns properties") {
    MockTransport transport;
    Protocol proto(&transport);

    transport.queue({{"type", "button"}, {"x", 10}, {"text", "OK"}});
    auto props = proto.get_props("btn_ok");
    CHECK(props["type"] == "button");
    CHECK(props["x"] == 10);
}

// ============================================================
// Protocol::get_screen_info
// ============================================================

TEST_CASE("Protocol::get_screen_info parses response") {
    MockTransport transport;
    Protocol proto(&transport);

    transport.queue({{"width", 800}, {"height", 480}, {"color_format", "ARGB8888"}});
    auto info = proto.get_screen_info();
    CHECK(info.width == 800);
    CHECK(info.height == 480);
    CHECK(info.color_format == "ARGB8888");
}

// ============================================================
// Protocol command serialization
// ============================================================

TEST_CASE("Protocol::press sends correct command") {
    MockTransport transport;
    Protocol proto(&transport);

    transport.queue({{"success", true}});
    proto.press(50, 75);

    auto cmd = json::parse(transport.sent[0]);
    CHECK(cmd["cmd"] == "press");
    CHECK(cmd["x"] == 50);
    CHECK(cmd["y"] == 75);
}

TEST_CASE("Protocol::swipe sends correct command") {
    MockTransport transport;
    Protocol proto(&transport);

    transport.queue({{"success", true}});
    proto.swipe(10, 20, 100, 200, 300);

    auto cmd = json::parse(transport.sent[0]);
    CHECK(cmd["cmd"] == "swipe");
    CHECK(cmd["x"] == 10);
    CHECK(cmd["y"] == 20);
    CHECK(cmd["x_end"] == 100);
    CHECK(cmd["y_end"] == 200);
    CHECK(cmd["duration"] == 300);
}

TEST_CASE("Protocol::type_text sends text") {
    MockTransport transport;
    Protocol proto(&transport);

    transport.queue({{"success", true}});
    proto.type_text("hello");

    auto cmd = json::parse(transport.sent[0]);
    CHECK(cmd["cmd"] == "type");
    CHECK(cmd["text"] == "hello");
}

TEST_CASE("Protocol::key sends key code") {
    MockTransport transport;
    Protocol proto(&transport);

    transport.queue({{"success", true}});
    proto.key("ENTER");

    auto cmd = json::parse(transport.sent[0]);
    CHECK(cmd["cmd"] == "key");
    CHECK(cmd["key"] == "ENTER");
}

// ============================================================
// Protocol::fire_and_forget invalidates cache
// ============================================================

TEST_CASE("click invalidates tree cache") {
    MockTransport transport;
    Protocol proto(&transport);

    // Populate cache
    transport.queue({{"tree", {{"type", "obj"}}}});
    proto.get_tree_cached(5000);
    CHECK(transport.sent.size() == 1);

    // Click invalidates
    transport.queue({{"success", true}});
    proto.click("btn");

    // Next tree fetch must re-send
    transport.queue({{"tree", {{"type", "obj"}}}});
    proto.get_tree_cached(5000);
    CHECK(transport.sent.size() == 3);  // get_tree + click + get_tree
}
