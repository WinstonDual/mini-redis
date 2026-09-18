#include "net/socket.hpp"

#include <atomic>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "resp/parser.hpp"
#include "resp/serializer.hpp"
#include "server/command.hpp"

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

namespace {

std::mutex g_log_mutex;

/// Потокобезопасный вывод строки в stdout.
void log_line(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::cout << msg << "\n";
}

} // namespace

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

    log_line("[mini-redis] listening on port " + std::to_string(port_));
}

TcpServer::~TcpServer() {
    if (listen_sock_ != INVALID_SOCK) {
        close_socket(listen_sock_);
    }
}

namespace {

std::atomic<std::uint64_t> g_client_id_counter{0};

/// Обслуживание одного клиента: читает байты, парсит RESP, отвечает.
/// Выполняется в отдельном потоке.
void serve_client(socket_t client, std::uint64_t client_id) {
    resp::RespParser parser;
    server::CommandDispatcher dispatcher;

    char buf[4096];

    for (;;) {
        int n = ::recv(client, buf, sizeof(buf), 0);
        if (n <= 0) {
            break; // клиент отключился или ошибка
        }

        parser.feed(std::string_view(buf, static_cast<std::size_t>(n)));

        try {
            while (auto value = parser.try_parse()) {
                auto reply = dispatcher.dispatch(*value);
                std::string bytes = resp::serialize(reply);
                if (::send(client, bytes.data(),
                           static_cast<int>(bytes.size()), 0) <= 0) {
                    log_line("[mini-redis] client #" + std::to_string(client_id)
                             + " send failed, disconnecting");
                    return;
                }
            }
        } catch (const resp::ProtocolError& e) {
            std::string err = resp::serialize(
                resp::make_error(std::string("ERR Protocol error: ") + e.what()));
            ::send(client, err.data(), static_cast<int>(err.size()), 0);
            return;
        }
    }
}

} // namespace

void TcpServer::listen_and_serve() {
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

        std::uint64_t id = ++g_client_id_counter;
        log_line("[mini-redis] client #" + std::to_string(id) + " connected");

        // Запускаем поток, который сам закроет сокет в конце.
        std::thread([client, id]() {
            serve_client(client, id);
            close_socket(client);
            log_line("[mini-redis] client #" + std::to_string(id) + " disconnected");
        }).detach();
    }
}

} // namespace miniredis::net