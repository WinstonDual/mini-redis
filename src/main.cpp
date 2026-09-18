#include "net/socket.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    std::uint16_t port = 6379;
    if (argc > 1) {
        try {
            int p = std::stoi(argv[1]);
            if (p <= 0 || p > 65535) throw std::out_of_range("port");
            port = static_cast<std::uint16_t>(p);
        } catch (...) {
            std::cerr << "Invalid port: " << argv[1] << "\n";
            return 1;
        }
    }

    try {
        miniredis::net::global_init();
        {
            miniredis::net::TcpServer server(port);
            server.listen_and_serve();
        }
        miniredis::net::global_cleanup();
    } catch (const std::exception& e) {
        std::cerr << "[mini-redis] fatal: " << e.what() << "\n";
        miniredis::net::global_cleanup();
        return 1;
    }
    return 0;
}