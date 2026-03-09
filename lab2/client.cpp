#include "message_io.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

static void receiver_loop(int fd, bool& running) {
    try {
        while (running) {
            uint8_t type = 0;
            std::string payload;
            bool ok = recv_message(fd, type, payload);
            if (!ok) {
                std::cout << "Disconnected\n";
                running = false;
                break;
            }

            if (type == MSG_TEXT) {
                std::cout << payload << "\n";
            } else if (type == MSG_PONG) {
                std::cout << "PONG\n";
            } else if (type == MSG_WELCOME) {
                std::cout << payload << "\n";
            } else if (type == MSG_BYE) {
                std::cout << "Disconnected\n";
                running = false;
                break;
            } else {
                std::cout << "(unknown type " << int(type) << ")\n";
            }

            std::cout << "> " << std::flush;
        }
    } catch (const std::exception& e) {
        std::cerr << "Receiver error: " << e.what() << "\n";
        running = false;
    }
}

int main(int argc, char* argv[]) {
    std::string server_ip = "127.0.0.1";
    int server_port = 9090;

    if (argc > 1) server_ip = argv[1];
    if (argc > 2) server_port = std::stoi(argv[2]);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "socket() failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip.c_str(), &srv.sin_addr) <= 0) {
        std::cerr << "inet_pton() failed for IP: " << server_ip << "\n";
        close(fd);
        return 1;
    }

    if (connect(fd, reinterpret_cast<sockaddr*>(&srv), sizeof(srv)) < 0) {
        std::cerr << "connect() failed: " << std::strerror(errno) << "\n";
        close(fd);
        return 1;
    }

    std::cout << "Connected\n";

    std::cout << "Enter nick: ";
    std::string nick;
    std::getline(std::cin, nick);
    if (nick.empty()) nick = "anonymous";

    send_message(fd, MSG_HELLO, nick);

    uint8_t t = 0;
    std::string p;
    if (!recv_message(fd, t, p)) {
        std::cout << "Disconnected\n";
        close(fd);
        return 0;
    }
    if (t != MSG_WELCOME) {
        std::cerr << "Expected WELCOME, got type=" << int(t) << "\n";
        close(fd);
        return 1;
    }

    std::cout << p << "\n";

    bool running = true;
    std::thread recv_thread(receiver_loop, fd, std::ref(running));

    std::cout << "> " << std::flush;
    while (running) {
        std::string line;
        if (!std::getline(std::cin, line)) break;

        if (line == "/ping") {
            send_message(fd, MSG_PING, "");
        } else if (line == "/quit") {
            send_message(fd, MSG_BYE, "");
        } else {
            send_message(fd, MSG_TEXT, line);
        }

        std::cout << "> " << std::flush;
    }

    running = false;
    shutdown(fd, SHUT_RDWR);
    close(fd);

    if (recv_thread.joinable()) recv_thread.join();
    return 0;
}