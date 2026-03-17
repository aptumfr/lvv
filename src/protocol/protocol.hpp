#pragma once

#include "commands.hpp"
#include "core/screen_capture.hpp"
#include "transport/transport.hpp"
#include <json.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace lvv {

class Protocol {
public:
    explicit Protocol(ITransport* transport);

    bool connect();
    void disconnect();
    bool is_connected() const;

    // Commands matching the spy protocol
    std::string ping();
    nlohmann::json get_tree(const std::string& root = "");
    std::optional<WidgetInfo> find(const std::string& selector);
    bool click(const std::string& selector);
    bool click_at(int x, int y);
    bool press(int x, int y);
    bool release();
    bool move_to(int x, int y);
    bool swipe(int x1, int y1, int x2, int y2, int duration_ms);
    bool type_text(const std::string& text);
    bool key(const std::string& key_code);
    Image screenshot();
    nlohmann::json get_props(const std::string& selector, const std::string& prop = "");
    ScreenInfo get_screen_info();

    // Compound gestures (implemented client-side, no new protocol commands)
    // The optional cancel callback is polled during sleeps; returning true aborts the gesture.
    using CancelFn = std::function<bool()>;
    bool long_press(int x, int y, int duration_ms = 500, CancelFn cancel = nullptr);
    bool drag(int x1, int y1, int x2, int y2, int duration_ms = 300, int steps = 10,
              CancelFn cancel = nullptr);

private:
    nlohmann::json send_command(const nlohmann::json& cmd);
    bool fire_and_forget(const nlohmann::json& cmd);
    static WidgetInfo parse_widget(const nlohmann::json& j);

    ITransport* transport_;
    mutable std::mutex mutex_;
};

} // namespace lvv
