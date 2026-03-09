#include "message_io.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static std::string addr_to_string(const sockaddr_in& addr) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    int port = ntohs(addr.sin_port);
    return std::string(ip) + ":" + std::to_string(port);
}

int main(int argc, char* argv[]) {
    int port = 9090;
    if (argc > 1) port = std::stoi(argv[1]);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cerr << "socket() failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = INADDR_ANY;
    srv.sin_port = htons(port);

    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&srv), sizeof(srv)) < 0) {
        std::cerr << "bind() failed: " << std::strerror(errno) << "\n";
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 1) < 0) {
        std::cerr << "listen() failed: " << std::strerror(errno) << "\n";
        close(listen_fd);
        return 1;
    }

    std::cout << "Server listening on port " << port << "\n";

    sockaddr_in client{};
    socklen_t client_len = sizeof(client);
    int client_fd = accept(listen_fd, reinterpret_cast<sockaddr*>(&client), &client_len);
    if (client_fd < 0) {
        std::cerr << "accept() failed: " << std::strerror(errno) << "\n";
        close(listen_fd);
        return 1;
    }

    std::string client_str = addr_to_string(client);
    std::cout << "Client connected\n";

    try {
        uint8_t type = 0;
        std::string payload;

        if (!recv_message(client_fd, type, payload)) {
            std::cout << "Client disconnected\n";
            close(client_fd);
            close(listen_fd);
            return 0;
        }

        if (type != MSG_HELLO) {
            std::cerr << "Expected MSG_HELLO, got type=" << int(type) << "\n";
            send_message(client_fd, MSG_BYE, "");
            close(client_fd);
            close(listen_fd);
            return 1;
        }

        std::cout << "[" << client_str << "]: " << payload << "\n";

        send_message(client_fd, MSG_WELCOME, "Welcome " + client_str);

        while (true) {
            uint8_t t = 0;
            std::string p;

            bool ok = recv_message(client_fd, t, p);
            if (!ok) {
                std::cout << "Client disconnected\n";
                break;
            }

            if (t == MSG_TEXT) {
                std::cout << "[" << client_str << "]: " << p << "\n";
            } else if (t == MSG_PING) {
                send_message(client_fd, MSG_PONG, "");
            } else if (t == MSG_BYE) {
                send_message(client_fd, MSG_BYE, "");
                std::cout << "Client disconnected\n";
                break;
            } else {
                std::cout << "[" << client_str << "]: " << "(unknown type " << int(t) << ")\n";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << "\n";
    }

    close(client_fd);
    close(listen_fd);
    return 0;
}