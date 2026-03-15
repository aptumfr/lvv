#include "ws_handler.hpp"
#include "protocol/protocol.hpp"
#include "core/screen_capture.hpp"

#include <json.hpp>
#include <chrono>

namespace lvv {

WSHandler::WSHandler() = default;

WSHandler::~WSHandler() {
    stop_streaming();
}

void WSHandler::setup(crow::App<crow::CORSHandler>& app, Protocol* protocol) {
    protocol_ = protocol;

    CROW_WEBSOCKET_ROUTE(app, "/ws")
        .onopen([this](crow::websocket::connection& conn) {
            std::lock_guard lock(mutex_);
            clients_.insert(&conn);
        })
        .onclose([this](crow::websocket::connection& conn,
                         const std::string&, uint16_t) {
            std::lock_guard lock(mutex_);
            clients_.erase(&conn);
        })
        .onmessage([this](crow::websocket::connection&,
                           const std::string& data, bool) {
            auto j = nlohmann::json::parse(data, nullptr, false);
            if (j.is_discarded()) return;

            auto type = j.value("type", "");
            if (type == "start_stream") {
                int fps = j.value("fps", 10);
                start_streaming(fps);
            } else if (type == "stop_stream") {
                stop_streaming();
            } else {
                handle_interaction(j);
            }
        });
}

void WSHandler::handle_interaction(const nlohmann::json& j) {
    if (!protocol_ || !protocol_->is_connected()) return;

    auto type = j.value("type", "");
    try {
        if (type == "press") {
            protocol_->press(j.value("x", 0), j.value("y", 0));
        } else if (type == "release") {
            protocol_->release();
        } else if (type == "move_to") {
            protocol_->move_to(j.value("x", 0), j.value("y", 0));
        } else if (type == "click_at") {
            protocol_->click_at(j.value("x", 0), j.value("y", 0));
        } else if (type == "click") {
            protocol_->click(j.value("name", ""));
        } else if (type == "swipe") {
            protocol_->swipe(
                j.value("x", 0), j.value("y", 0),
                j.value("x_end", 0), j.value("y_end", 0),
                j.value("duration", 300));
        } else if (type == "type") {
            protocol_->type_text(j.value("text", ""));
        } else if (type == "key") {
            protocol_->key(j.value("key", ""));
        }
    } catch (...) {
        // Best-effort
    }
}

void WSHandler::broadcast(const std::string& message) {
    std::lock_guard lock(mutex_);
    for (auto* conn : clients_) {
        conn->send_text(message);
    }
}

void WSHandler::broadcast_binary(const std::vector<uint8_t>& data) {
    std::string msg(reinterpret_cast<const char*>(data.data()), data.size());
    std::lock_guard lock(mutex_);
    for (auto* conn : clients_) {
        conn->send_binary(msg);
    }
}

void WSHandler::start_streaming(int fps) {
    if (streaming_.load()) return;
    if (!protocol_ || !protocol_->is_connected()) return;

    streaming_ = true;
    auto interval = std::chrono::milliseconds(1000 / std::max(fps, 1));

    stream_thread_ = std::thread([this, interval]() {
        auto next_frame = std::chrono::steady_clock::now();
        while (streaming_.load()) {
            try {
                auto img = protocol_->screenshot();
                if (img.valid()) {
                    auto png = encode_png(img);
                    broadcast_binary(png);
                }
            } catch (...) {
                // Target disconnected or error — keep trying
            }
            // Deadline-based pacing: skip frames if capture took too long
            next_frame += interval;
            auto now = std::chrono::steady_clock::now();
            if (next_frame > now) {
                std::this_thread::sleep_until(next_frame);
            } else {
                next_frame = now;  // we're behind, reset deadline
            }
        }
    });
}

void WSHandler::stop_streaming() {
    streaming_ = false;
    if (stream_thread_.joinable()) {
        stream_thread_.join();
    }
}

size_t WSHandler::client_count() const {
    auto& self = const_cast<WSHandler&>(*this);
    std::lock_guard lock(self.mutex_);
    return self.clients_.size();
}

} // namespace lvv
