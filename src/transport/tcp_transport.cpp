#include "tcp_transport.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <algorithm>

namespace lvv {

TCPTransport::TCPTransport(std::string host, int port)
    : host_(std::move(host)), port_(port) {}

TCPTransport::~TCPTransport() {
    disconnect();
}

bool TCPTransport::connect() {
    if (fd_ >= 0) return true;

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    auto port_str = std::to_string(port_);
    if (getaddrinfo(host_.c_str(), port_str.c_str(), &hints, &res) != 0) {
        return false;
    }

    for (auto* rp = res; rp; rp = rp->ai_next) {
        fd_ = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd_ < 0) continue;

        if (::connect(fd_, rp->ai_addr, rp->ai_addrlen) == 0) {
            freeaddrinfo(res);
            buffer_.clear();
            return true;
        }
        ::close(fd_);
        fd_ = -1;
    }

    freeaddrinfo(res);
    return false;
}

void TCPTransport::abort() {
    // shutdown() unblocks any thread blocked in poll()/recv() on this fd.
    // Does not close the fd or mutate any other state — safe to call concurrently.
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
    }
}

void TCPTransport::disconnect() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    buffer_.clear();
}

bool TCPTransport::is_connected() const {
    return fd_ >= 0;
}

bool TCPTransport::send(std::string_view data) {
    if (fd_ < 0) return false;

    std::string msg(data);
    if (msg.empty() || msg.back() != '\n') {
        msg.push_back('\n');
    }

    size_t total = 0;
    while (total < msg.size()) {
        auto n = ::send(fd_, msg.data() + total, msg.size() - total, MSG_NOSIGNAL);
        if (n <= 0) {
            disconnect();
            return false;
        }
        total += n;
    }
    return true;
}

std::optional<std::string> TCPTransport::receive(std::chrono::milliseconds timeout) {
    if (fd_ < 0) return std::nullopt;

    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        // Check if we already have a complete line in the buffer
        auto nl = buffer_.find('\n');
        if (nl != std::string::npos) {
            std::string line = buffer_.substr(0, nl);
            buffer_.erase(0, nl + 1);
            return line;
        }

        // Calculate remaining timeout
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return std::nullopt;
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);

        struct pollfd pfd{};
        pfd.fd = fd_;
        pfd.events = POLLIN;

        int ret = ::poll(&pfd, 1, static_cast<int>(remaining.count()));
        if (ret <= 0) return std::nullopt;

        char buf[65536];
        auto n = ::recv(fd_, buf, sizeof(buf), 0);
        if (n <= 0) {
            disconnect();
            return std::nullopt;
        }
        buffer_.append(buf, n);

        // Guard against unbounded buffer growth (32 MB cap)
        if (buffer_.size() > 32 * 1024 * 1024) {
            disconnect();
            return std::nullopt;
        }
    }
}

std::optional<std::vector<uint8_t>> TCPTransport::receive_bytes(
    size_t count, std::chrono::milliseconds timeout) {
    if (fd_ < 0 || count == 0) return std::nullopt;

    auto deadline = std::chrono::steady_clock::now() + timeout;

    std::vector<uint8_t> result;
    result.reserve(count);

    // Drain from existing buffer first
    size_t from_buf = std::min(count, buffer_.size());
    if (from_buf > 0) {
        result.insert(result.end(), buffer_.begin(), buffer_.begin() + from_buf);
        buffer_.erase(0, from_buf);
    }

    // Read remaining from socket
    while (result.size() < count) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return std::nullopt;
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);

        struct pollfd pfd{};
        pfd.fd = fd_;
        pfd.events = POLLIN;

        int ret = ::poll(&pfd, 1, static_cast<int>(remaining.count()));
        if (ret <= 0) return std::nullopt;

        size_t need = count - result.size();
        char buf[65536];
        size_t to_read = std::min(need, sizeof(buf));
        auto n = ::recv(fd_, buf, to_read, 0);
        if (n <= 0) {
            disconnect();
            return std::nullopt;
        }
        result.insert(result.end(), buf, buf + n);
    }

    return result;
}

} // namespace lvv
