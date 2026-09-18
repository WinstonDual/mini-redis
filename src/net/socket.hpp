#pragma once

#include <cstdint>
#include <string>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
    constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
#else
    using socket_t = int;
    constexpr socket_t INVALID_SOCK = -1;
#endif

namespace miniredis::net {

/// Инициализация сетевой подсистемы (WSAStartup на Windows).
void global_init();

/// Освобождение ресурсов (WSACleanup на Windows).
void global_cleanup();

/// Простейший TCP-сервер (пока блокирующий, один клиент за раз).
class TcpServer {
public:
    explicit TcpServer(std::uint16_t port);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    void listen_and_serve();

private:
    socket_t listen_sock_ = INVALID_SOCK;
    std::uint16_t port_;
};

} // namespace miniredis::net