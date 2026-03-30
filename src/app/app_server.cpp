/// Web UI server mode

#include "app.hpp"
#include "core/widget_tree.hpp"
#include "core/test_runner.hpp"
#include "scripting/script_engine.hpp"
#include "server/web_server.hpp"
#include "core/log.hpp"

namespace lvv {

int App::serve(const AppConfig& config) {
    if (!connect(config)) {
        LOG_WARNING(log::get(), "Could not connect to target. Server starting anyway.");
    }

    script_engine_ = std::make_unique<ScriptEngine>();
    script_engine_->set_timeout(config.timeout);
    if (protocol_) {
        script_engine_->set_protocol(protocol_.get());
    }
    script_engine_->set_visual_defaults(config.ref_images_dir, config.diff_threshold);

    widget_tree_ = std::make_unique<WidgetTree>();
    test_runner_ = std::make_unique<TestRunner>(*script_engine_);

    web_server_ = std::make_unique<WebServer>(
        protocol_.get(), widget_tree_.get(),
        script_engine_.get(), test_runner_.get(),
        config.static_dir, config.ref_images_dir);

    LOG_INFO(log::get(), "LVV Server v0.1.0");
    if (!config.serial_device.empty()) {
        LOG_INFO(log::get(), "Target: {} @ {} baud", config.serial_device, config.serial_baud);
    } else {
        LOG_INFO(log::get(), "Target: {}:{}", config.target_host, config.target_port);
    }

    web_server_->start(config.web_port, false);

    return 0;
}

} // namespace lvv
