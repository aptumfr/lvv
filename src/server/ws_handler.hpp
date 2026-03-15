#pragma once

#include <crow_all.h>
#include <json.hpp>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <atomic>
#include <thread>

namespace lvv {

class Protocol;

class WSHandler {
public:
    WSHandler();
    ~WSHandler();

    /// Set up WebSocket route on the app
    void setup(crow::App<crow::CORSHandler>& app, Protocol* protocol);

    /// Broadcast a text message to all connected clients
    void broadcast(const std::string& message);

    /// Broadcast binary data to all connected clients
    void broadcast_binary(const std::vector<uint8_t>& data);

    /// Start streaming screenshots at given FPS
    void start_streaming(int fps);

    /// Stop streaming
    void stop_streaming();

    bool is_streaming() const { return streaming_.load(); }
    size_t client_count() const;

private:
    void handle_interaction(const nlohmann::json& j);

    std::mutex mutex_;
    std::set<crow::websocket::connection*> clients_;

    std::atomic<bool> streaming_{false};
    std::thread stream_thread_;
    Protocol* protocol_ = nullptr;
};

} // namespace lvv
