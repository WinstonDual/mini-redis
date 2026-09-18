#include "net/socket.hpp"

#include <cstring>
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #include <cerrno>
#endif

namespace miniredis::net {

// ---------- helpers ----------

#ifdef _WIN32
using socklen_t = int;
#endif

static std::string last_error() {
#ifdef _WIN32
    int err = WSAGetLastError();
    char buf[256] = {0};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, buf, sizeof(buf), nullptr);
    return std::string(buf);
#else
    return std::string(std::strerror(errno));
#endif
}

static void close_socket(socket_t s) {
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
}

// ---------- public API ----------

void global_init() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
#endif
}

void global_cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// ---------- TcpServer ----------

TcpServer::TcpServer(std::uint16_t port) : port_(port) {
    listen_sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock_ == INVALID_SOCK) {
        throw std::runtime_error("socket() failed: " + last_error());
    }

    int opt = 1;
    setsockopt(listen_sock_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (::bind(listen_sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        auto msg = "bind() failed: " + last_error();
        close_socket(listen_sock_);
        throw std::runtime_error(msg);
    }

    if (::listen(listen_sock_, SOMAXCONN) != 0) {
        auto msg = "listen() failed: " + last_error();
        close_socket(listen_sock_);
        throw std::runtime_error(msg);
    }

    std::cout << "[mini-redis] listening on port " << port_ << "\n";
}

TcpServer::~TcpServer() {
    if (listen_sock_ != INVALID_SOCK) {
        close_socket(listen_sock_);
    }
}

void TcpServer::listen_and_serve() {
    // ШАГ 1: наивный блокирующий accept — один клиент за раз.
    // ШАГ 4: заменим на select/epoll + пул потоков.
    for (;;) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        socket_t client = ::accept(listen_sock_,
                                   reinterpret_cast<sockaddr*>(&client_addr),
                                   &client_len);
        if (client == INVALID_SOCK) {
            std::cerr << "[mini-redis] accept() failed: " << last_error() << "\n";
            continue;
        }

        std::cout << "[mini-redis] client connected\n";

        // Пока отвечаем +PONG (валидный RESP) на любое подключение.
        const char reply[] = "+PONG\r\n";
        ::send(client, reply, sizeof(reply) - 1, 0);

        close_socket(client);
        std::cout << "[mini-redis] client disconnected\n";
    }
}

} // namespace miniredis::net