#pragma once

#include "transport.hpp"
#include <string>

namespace lvv {

class TCPTransport : public ITransport {
public:
    TCPTransport(std::string host = "localhost", int port = 5555);
    ~TCPTransport() override;

    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;
    bool send(std::string_view data) override;
    std::optional<std::string> receive(std::chrono::milliseconds timeout) override;
    std::optional<std::vector<uint8_t>> receive_bytes(
        size_t count, std::chrono::milliseconds timeout) override;

private:
    std::string host_;
    int port_;
    int fd_ = -1;
    std::string buffer_;
};

} // namespace lvv
