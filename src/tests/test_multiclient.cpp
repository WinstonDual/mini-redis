#include "tests/mini_test.hpp"
#include "net/socket.hpp"
#include "resp/parser.hpp"
#include "resp/serializer.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

using namespace miniredis;

namespace {

// Подключиться к localhost:port. Возвращает сокет или INVALID_SOCK.
socket_t connect_to(std::uint16_t port) {
    socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCK) return INVALID_SOCK;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#ifdef _WIN32
        closesocket(s);
#else
        ::close(s);
#endif
        return INVALID_SOCK;
    }
    return s;
}

void close_conn(socket_t s) {
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
}

// Отправить PING, ожидать +PONG\r\n
bool ping_once(socket_t s) {
    const char req[] = "*1\r\n$4\r\nPING\r\n";
    if (::send(s, req, sizeof(req) - 1, 0) <= 0) return false;

    char buf[128];
    int n = ::recv(s, buf, sizeof(buf), 0);
    if (n <= 0) return false;

    std::string_view reply(buf, static_cast<std::size_t>(n));
    return reply.rfind("+PONG", 0) == 0;
}

} // namespace

TEST(multiclient_parallel_pings) {
    // Стартуем сервер в отдельном потоке на порту 16379.
    constexpr std::uint16_t PORT = 16379;
    std::atomic<bool> ready{false};

    std::thread server_thread([&]() {
        net::global_init();
        net::TcpServer server(PORT);
        ready = true;
        server.listen_and_serve();
        // Остановить сервер нельзя — он крутится в бесконечном accept.
        // Тестовый поток умрёт вместе с процессом.
    });
    server_thread.detach();

    // Ждём готовности сервера
    for (int i = 0; i < 100 && !ready; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(ready);

    // Запускаем 10 клиентов, каждый сделает 5 PING-ов.
    constexpr int CLIENTS = 10;
    constexpr int PINGS_PER_CLIENT = 5;

    std::atomic<int> success{0};
    std::vector<std::thread> clients;
    clients.reserve(CLIENTS);

    for (int i = 0; i < CLIENTS; ++i) {
        clients.emplace_back([&]() {
            socket_t s = connect_to(PORT);
            if (s == INVALID_SOCK) return;

            for (int j = 0; j < PINGS_PER_CLIENT; ++j) {
                if (ping_once(s)) ++success;
            }
            close_conn(s);
        });
    }

    for (auto& t : clients) t.join();

    CHECK_EQ(success.load(), CLIENTS * PINGS_PER_CLIENT);
}