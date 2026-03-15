#include "serial_transport.hpp"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <cstring>

namespace lvv {

SerialTransport::SerialTransport(std::string device, int baud_rate)
    : device_(std::move(device)), baud_rate_(baud_rate) {}

SerialTransport::~SerialTransport() {
    disconnect();
}

int SerialTransport::baud_to_speed(int baud) {
    switch (baud) {
        case 9600:    return B9600;
        case 19200:   return B19200;
        case 38400:   return B38400;
        case 57600:   return B57600;
        case 115200:  return B115200;
        case 230400:  return B230400;
        case 460800:  return B460800;
        case 921600:  return B921600;
        default:      return B115200;
    }
}

bool SerialTransport::connect() {
    if (fd_ >= 0) return true;

    fd_ = ::open(device_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) return false;

    // Clear non-blocking for normal operation
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);

    struct termios tty{};
    if (tcgetattr(fd_, &tty) != 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    auto speed = baud_to_speed(baud_rate_);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // 8N1
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    // No flow control
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    // Raw mode
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~OPOST;

    // Read returns immediately with whatever is available
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1; // 100ms timeout

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    // Flush
    tcflush(fd_, TCIOFLUSH);
    buffer_.clear();
    return true;
}

void SerialTransport::disconnect() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    buffer_.clear();
}

bool SerialTransport::is_connected() const {
    return fd_ >= 0;
}

bool SerialTransport::send(std::string_view data) {
    if (fd_ < 0) return false;

    // STX/ETX framing: STX + payload + ETX
    std::string frame;
    frame.reserve(data.size() + 2);
    frame.push_back(static_cast<char>(STX));
    frame.append(data);
    // Strip trailing newline if present (we use ETX as delimiter instead)
    if (!frame.empty() && frame.back() == '\n') {
        frame.pop_back();
    }
    frame.push_back(static_cast<char>(ETX));

    size_t total = 0;
    while (total < frame.size()) {
        auto n = ::write(fd_, frame.data() + total, frame.size() - total);
        if (n <= 0) {
            disconnect();
            return false;
        }
        total += n;
    }

    // Drain output
    tcdrain(fd_);
    return true;
}

std::optional<std::string> SerialTransport::receive(std::chrono::milliseconds timeout) {
    if (fd_ < 0) return std::nullopt;

    auto deadline = std::chrono::steady_clock::now() + timeout;

    // Look for a complete STX...ETX frame
    while (true) {
        // Check for complete frame in buffer
        auto stx_pos = buffer_.find(static_cast<char>(STX));
        if (stx_pos != std::string::npos) {
            auto etx_pos = buffer_.find(static_cast<char>(ETX), stx_pos + 1);
            if (etx_pos != std::string::npos) {
                std::string payload = buffer_.substr(stx_pos + 1, etx_pos - stx_pos - 1);
                buffer_.erase(0, etx_pos + 1);
                return payload;
            }
        }

        // Check timeout
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return std::nullopt;
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);

        // Wait for data
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd_, &fds);

        struct timeval tv;
        tv.tv_sec = remaining.count() / 1000;
        tv.tv_usec = (remaining.count() % 1000) * 1000;

        int ret = ::select(fd_ + 1, &fds, nullptr, nullptr, &tv);
        if (ret <= 0) return std::nullopt;

        char buf[1024];
        auto n = ::read(fd_, buf, sizeof(buf));
        if (n <= 0) {
            disconnect();
            return std::nullopt;
        }
        buffer_.append(buf, n);

        // Guard against unbounded buffer growth (8 MB cap for serial)
        if (buffer_.size() > 8 * 1024 * 1024) {
            disconnect();
            return std::nullopt;
        }
    }
}

std::optional<std::vector<uint8_t>> SerialTransport::receive_bytes(
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

    // Read remaining from fd
    while (result.size() < count) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return std::nullopt;
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd_, &fds);

        struct timeval tv;
        tv.tv_sec = remaining.count() / 1000;
        tv.tv_usec = (remaining.count() % 1000) * 1000;

        int ret = ::select(fd_ + 1, &fds, nullptr, nullptr, &tv);
        if (ret <= 0) return std::nullopt;

        size_t need = count - result.size();
        char buf[4096];
        size_t to_read = need < sizeof(buf) ? need : sizeof(buf);
        auto n = ::read(fd_, buf, to_read);
        if (n <= 0) {
            disconnect();
            return std::nullopt;
        }
        result.insert(result.end(), buf, buf + n);
    }

    return result;
}

} // namespace lvv
