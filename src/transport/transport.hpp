#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lvv {

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;

    /// Signal the transport to abort any blocking I/O. Thread-safe.
    /// Does NOT close or clean up — call disconnect() afterwards.
    virtual void abort() = 0;

    /// Send data (newline-delimited JSON line)
    virtual bool send(std::string_view data) = 0;

    /// Receive one newline-delimited line, with timeout
    virtual std::optional<std::string> receive(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) = 0;

    /// Receive exactly count raw bytes, with timeout
    virtual std::optional<std::vector<uint8_t>> receive_bytes(
        size_t count,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(10000)) = 0;
};

} // namespace lvv
