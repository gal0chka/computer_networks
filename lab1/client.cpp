#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    std::string server_ip = "127.0.0.1";
    int server_port = 8080;

    if (argc > 1) {
        server_ip = argv[1];
    }
    if (argc > 2) {
        server_port = std::stoi(argv[2]);
    }

    int client_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_fd < 0) {
        std::cerr << "socket() failed: " << std::strerror(errno) << '\n';
        return 1;
    }

    // Таймаут ожидания ответа (необязательно, но удобно)
    timeval timeout{};
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "inet_pton() failed for IP: " << server_ip << '\n';
        close(client_fd);
        return 1;
    }

    std::cout << "UDP client -> server " << server_ip << ":" << server_port << '\n';
    std::cout << "Enter message (type 'exit' to quit):\n";

    constexpr size_t BUFFER_SIZE = 4096;
    char buffer[BUFFER_SIZE];

    while (true) {
        std::string message;
        std::getline(std::cin, message);

        if (!std::cin) {
            break;
        }
        if (message == "exit") {
            break;
        }

        ssize_t sent = sendto(
            client_fd,
            message.c_str(),
            message.size(),
            0,
            reinterpret_cast<sockaddr*>(&server_addr),
            sizeof(server_addr)
        );

        if (sent < 0) {
            std::cerr << "sendto() failed: " << std::strerror(errno) << '\n';
            continue;
        }

        sockaddr_in from_addr{};
        socklen_t from_len = sizeof(from_addr);

        ssize_t received = recvfrom(
            client_fd,
            buffer,
            BUFFER_SIZE - 1,
            0,
            reinterpret_cast<sockaddr*>(&from_addr),
            &from_len
        );

        if (received < 0) {
            std::cerr << "recvfrom() failed (timeout or error): " << std::strerror(errno) << '\n';
            continue;
        }

        buffer[received] = '\0';
        std::cout << "Echo from server: " << buffer << '\n';
    }

    close(client_fd);
    return 0;
}
