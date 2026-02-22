#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    int server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_fd < 0) {
        std::cerr << "socket() failed: " << std::strerror(errno) << '\n';
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // слушаем все интерфейсы
    server_addr.sin_port = htons(port);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        std::cerr << "bind() failed: " << std::strerror(errno) << '\n';
        close(server_fd);
        return 1;
    }

    std::cout << "UDP echo server started on port " << port << '\n';

    constexpr size_t BUFFER_SIZE = 4096;
    char buffer[BUFFER_SIZE];

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        ssize_t bytes_received = recvfrom(
            server_fd,
            buffer,
            BUFFER_SIZE - 1,
            0,
            reinterpret_cast<sockaddr*>(&client_addr),
            &client_len
        );

        if (bytes_received < 0) {
            std::cerr << "recvfrom() failed: " << std::strerror(errno) << '\n';
            continue;
        }

        buffer[bytes_received] = '\0';

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);

        std::cout << "Received from " << client_ip << ":" << client_port
                  << " -> \"" << buffer << "\"\n";

        ssize_t bytes_sent = sendto(
            server_fd,
            buffer,
            bytes_received,
            0,
            reinterpret_cast<sockaddr*>(&client_addr),
            client_len
        );

        if (bytes_sent < 0) {
            std::cerr << "sendto() failed: " << std::strerror(errno) << '\n';
        }
    }

    close(server_fd);
    return 0;
}
