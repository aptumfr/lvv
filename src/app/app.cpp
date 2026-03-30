/// App core: constructor, destructor, connection setup

#include "app.hpp"
#include "transport/tcp_transport.hpp"
#include "transport/serial_transport.hpp"
#include "protocol/protocol.hpp"
#include "core/widget_tree.hpp"
#include "core/test_runner.hpp"
#include "scripting/script_engine.hpp"
#include "server/web_server.hpp"
#include "core/log.hpp"

namespace lvv {

App::App() = default;
App::~App() = default;

bool App::connect(const AppConfig& config) {
    if (!config.serial_device.empty()) {
        transport_ = std::make_unique<SerialTransport>(
            config.serial_device, config.serial_baud);
        LOG_INFO(log::get(), "Connecting via serial: {} @ {} baud",
                 config.serial_device, config.serial_baud);
    } else {
        transport_ = std::make_unique<TCPTransport>(
            config.target_host, config.target_port);
        LOG_INFO(log::get(), "Connecting to {}:{}", config.target_host, config.target_port);
    }

    protocol_ = std::make_unique<Protocol>(transport_.get());

    if (!protocol_->connect()) {
        LOG_ERROR(log::get(), "Failed to connect");
        return false;
    }
    return true;
}

} // namespace lvv
