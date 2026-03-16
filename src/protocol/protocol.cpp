#include "protocol.hpp"

#include <stdexcept>

namespace lvv {

Protocol::Protocol(ITransport* transport)
    : transport_(transport) {}

bool Protocol::connect() {
    std::lock_guard lock(mutex_);
    return transport_->connect();
}

void Protocol::disconnect() {
    // Two-phase disconnect:
    // 1) abort() without the lock — calls shutdown(fd) which unblocks any
    //    thread stuck in recv()/poll() inside send_command()/screenshot(),
    //    causing it to error out and release the mutex.
    // 2) Acquire the lock, then close(fd) + clear buffers. The lock
    //    guarantees no command is mid-flight when state is mutated.
    transport_->abort();
    std::lock_guard lock(mutex_);
    transport_->disconnect();
}

bool Protocol::is_connected() const {
    std::lock_guard lock(mutex_);
    return transport_->is_connected();
}

nlohmann::json Protocol::send_command(const nlohmann::json& cmd) {
    std::lock_guard lock(mutex_);
    std::string msg = cmd.dump();
    if (!transport_->send(msg)) {
        throw std::runtime_error("Failed to send command");
    }

    auto response = transport_->receive();
    if (!response) {
        throw std::runtime_error("No response from target");
    }

    auto j = nlohmann::json::parse(*response, nullptr, false);
    if (j.is_discarded()) {
        throw std::runtime_error("Invalid JSON response");
    }

    if (j.contains("error")) {
        throw std::runtime_error("Target error: " + j["error"].get<std::string>());
    }

    return j;
}

std::string Protocol::ping() {
    auto resp = send_command({{"cmd", "ping"}});
    return resp.value("version", "unknown");
}

nlohmann::json Protocol::get_tree(const std::string& root) {
    nlohmann::json cmd = {{"cmd", "get_tree"}};
    if (!root.empty()) {
        cmd["root"] = root;
    }
    return send_command(cmd);
}

std::optional<WidgetInfo> Protocol::find(const std::string& selector) {
    try {
        auto resp = send_command({{"cmd", "find"}, {"name", selector}});
        if (resp.contains("widget")) {
            return parse_widget(resp["widget"]);
        }
    } catch (...) {}
    return std::nullopt;
}

bool Protocol::fire_and_forget(const nlohmann::json& cmd) {
    try {
        send_command(cmd);
        return true;
    } catch (...) {
        return false;
    }
}

bool Protocol::click(const std::string& selector) {
    return fire_and_forget({{"cmd", "click"}, {"name", selector}});
}

bool Protocol::click_at(int x, int y) {
    return fire_and_forget({{"cmd", "click_at"}, {"x", x}, {"y", y}});
}

bool Protocol::press(int x, int y) {
    return fire_and_forget({{"cmd", "press"}, {"x", x}, {"y", y}});
}

bool Protocol::release() {
    return fire_and_forget({{"cmd", "release"}});
}

bool Protocol::move_to(int x, int y) {
    return fire_and_forget({{"cmd", "move_to"}, {"x", x}, {"y", y}});
}

bool Protocol::swipe(int x1, int y1, int x2, int y2, int duration_ms) {
    return fire_and_forget({
        {"cmd", "swipe"},
        {"x", x1}, {"y", y1},
        {"x_end", x2}, {"y_end", y2},
        {"duration", duration_ms}
    });
}

bool Protocol::type_text(const std::string& text) {
    return fire_and_forget({{"cmd", "type"}, {"text", text}});
}

bool Protocol::key(const std::string& key_code) {
    return fire_and_forget({{"cmd", "key"}, {"key", key_code}});
}

Image Protocol::screenshot() {
    std::lock_guard lock(mutex_);

    std::string msg = R"({"cmd":"screenshot"})";
    if (!transport_->send(msg)) {
        throw std::runtime_error("Failed to send command");
    }

    auto header_line = transport_->receive();
    if (!header_line) {
        throw std::runtime_error("No response from target");
    }

    auto hdr = nlohmann::json::parse(*header_line, nullptr, false);
    if (hdr.is_discarded()) {
        throw std::runtime_error("Invalid JSON header");
    }
    if (hdr.contains("error")) {
        throw std::runtime_error("Target error: " + hdr["error"].get<std::string>());
    }

    int width = hdr.value("width", 0);
    int height = hdr.value("height", 0);
    int stride = hdr.value("stride", 0);
    int format = hdr.value("format", 0);
    size_t data_size = hdr.value("data_size", 0);

    if (width <= 0 || height <= 0 || stride <= 0 || data_size == 0) return {};
    if (width > 16384 || height > 16384) return {};
    if (data_size != static_cast<size_t>(stride) * height) {
        throw std::runtime_error("Screenshot header mismatch: data_size != stride * height");
    }

    auto raw = transport_->receive_bytes(data_size);
    if (!raw || raw->size() != data_size) {
        throw std::runtime_error("Failed to receive pixel data");
    }

    return decode_raw_pixels(*raw, width, height, stride, format);
}

nlohmann::json Protocol::get_props(const std::string& selector, const std::string& prop) {
    nlohmann::json cmd = {{"cmd", "get_props"}, {"name", selector}};
    if (!prop.empty()) {
        cmd["prop"] = prop;
    }
    return send_command(cmd);
}

ScreenInfo Protocol::get_screen_info() {
    auto resp = send_command({{"cmd", "get_screen_info"}});
    return ScreenInfo{
        .width = resp.value("width", 0),
        .height = resp.value("height", 0),
        .color_format = resp.value("color_format", "unknown")
    };
}

WidgetInfo Protocol::parse_widget(const nlohmann::json& j) {
    WidgetInfo w;
    w.id = j.value("id", 0);
    w.name = j.value("name", "");
    w.type = j.value("type", "");
    w.x = j.value("x", 0);
    w.y = j.value("y", 0);
    w.width = j.value("width", 0);
    w.height = j.value("height", 0);
    w.visible = j.value("visible", false);
    w.clickable = j.value("clickable", false);
    w.auto_path = j.value("auto_path", "");
    w.text = j.value("text", "");

    if (j.contains("children") && j["children"].is_array()) {
        for (const auto& child : j["children"]) {
            w.children.push_back(parse_widget(child));
        }
    }
    return w;
}

} // namespace lvv
