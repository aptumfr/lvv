#pragma once

#include "commands.hpp"
#include "core/screen_capture.hpp"
#include "transport/transport.hpp"
#include <json.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace lvv {

/// Thrown by the target when a widget is not found.
/// Distinct from std::runtime_error (transport/connection failure).
class widget_not_found : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Protocol {
public:
    explicit Protocol(ITransport* transport);

    bool connect();
    void disconnect();
    bool is_connected() const;

    // Error handling convention:
    //   - All methods throw std::runtime_error on transport failure (connection lost,
    //     send/receive error). Callers must catch or let it propagate.
    //   - find() returns nullopt when the widget doesn't exist (expected case).
    //   - Interaction methods (click, press, etc.) return false when the target
    //     reports a logical failure (e.g. widget not found for click-by-name).
    //   - Query methods (get_tree, screenshot, etc.) throw on any failure.

    std::string ping();
    nlohmann::json get_tree(const std::string& root = "");
    nlohmann::json get_tree_cached(int ttl_ms = 50);
    void invalidate_tree_cache();
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

    // Log capture
    nlohmann::json get_logs();
    bool clear_logs();
    bool set_log_capture(bool enable);

    // Performance metrics
    nlohmann::json get_metrics();

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

    // Tree cache (separate mutex — cache reads must not block protocol I/O)
    std::mutex cache_mutex_;
    nlohmann::json tree_cache_;
    std::chrono::steady_clock::time_point tree_cache_time_{};
};

} // namespace lvv
