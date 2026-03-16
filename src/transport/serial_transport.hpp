#pragma once

#include "transport.hpp"
#include <string>

namespace lvv {

class SerialTransport : public ITransport {
public:
    SerialTransport(std::string device = "/dev/ttyUSB0", int baud_rate = 115200);
    ~SerialTransport() override;

    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;
    void abort() override;
    bool send(std::string_view data) override;
    std::optional<std::string> receive(std::chrono::milliseconds timeout) override;
    std::optional<std::vector<uint8_t>> receive_bytes(
        size_t count, std::chrono::milliseconds timeout) override;

private:
    static int baud_to_speed(int baud);

    std::string device_;
    int baud_rate_;
    int fd_ = -1;
    std::string buffer_;

    static constexpr uint8_t STX = 0x02;
    static constexpr uint8_t ETX = 0x03;
};

} // namespace lvv
