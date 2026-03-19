#include "config.hpp"

#include <filesystem>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <climits>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

namespace lvv {

static std::string exe_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf)) {
        return std::filesystem::path(buf).parent_path().string();
    }
#elif defined(__linux__)
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return std::filesystem::path(buf).parent_path().string();
    }
#endif
    return std::filesystem::current_path().string();
}

std::string default_static_dir() {
    // Look for web/dist relative to the executable's directory
    // (works for both in-tree builds and installed layouts)
    auto dir = exe_dir();

    // In-tree build: <project>/build/lvv -> <project>/web/dist
    auto in_tree = std::filesystem::path(dir).parent_path() / "web" / "dist";
    if (std::filesystem::is_directory(in_tree)) {
        return in_tree.string();
    }

    // Installed layout: <prefix>/bin/lvv -> <prefix>/share/lvv/web
    auto installed = std::filesystem::path(dir).parent_path() / "share" / "lvv" / "web";
    if (std::filesystem::is_directory(installed)) {
        return installed.string();
    }

    // Last resort: relative to cwd
    return "web/dist";
}

std::string find_lvv_python_dir() {
    auto dir = exe_dir();

    // In-tree build: <project>/build/lvv -> <project>/python
    auto in_tree = std::filesystem::path(dir).parent_path() / "python";
    if (std::filesystem::exists(in_tree / "lvv.py")) {
        return in_tree.string();
    }

    // Installed: <prefix>/bin/lvv -> <prefix>/share/lvv/python
    auto installed = std::filesystem::path(dir).parent_path() / "share" / "lvv" / "python";
    if (std::filesystem::exists(installed / "lvv.py")) {
        return installed.string();
    }

    // Fallback: relative to cwd
    if (std::filesystem::exists("python/lvv.py")) {
        return std::filesystem::absolute("python").string();
    }

    return {};
}

int find_free_port() {
#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return 0;
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return 0;
#endif

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return 0;
    }

    socklen_t len = sizeof(addr);
    getsockname(sock, (struct sockaddr*)&addr, &len);
    int port = ntohs(addr.sin_port);

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return port;
}

} // namespace lvv
