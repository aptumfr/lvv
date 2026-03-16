#pragma once

#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include "quill/sinks/ConsoleSink.h"

namespace lvv {
namespace log {

/// Initialize the logging backend. Call once from main().
inline void init() {
    quill::Backend::start();

    auto sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console");
    auto logger = quill::Frontend::create_or_get_logger(
        "root", std::move(sink),
        quill::PatternFormatterOptions{
            "%(time) [%(log_level_short_code)] %(message)",
            "%H:%M:%S.%Qms"});

    logger->set_log_level(quill::LogLevel::Info);
}

/// Get the default logger
inline quill::Logger* get() {
    return quill::Frontend::get_logger("root");
}

/// Set log level (call after init)
inline void set_level(quill::LogLevel level) {
    get()->set_log_level(level);
}

} // namespace log
} // namespace lvv
