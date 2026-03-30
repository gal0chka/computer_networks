#ifndef COMMON_HPP
#define COMMON_HPP

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <queue>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

constexpr int MAX_PAYLOAD = 1024;
constexpr int THREAD_POOL_SIZE = 10;
constexpr int MAX_CLIENTS = 256;
constexpr int DEFAULT_PORT = 5555;
constexpr int RECONNECT_DELAY_SEC = 2;

enum MessageType : uint8_t {
    MSG_HELLO = 1,
    MSG_WELCOME = 2,
    MSG_TEXT = 3,
    MSG_PING = 4,
    MSG_PONG = 5,
    MSG_BYE = 6
};

struct Message {
    uint32_t length;
    uint8_t type;
    char payload[MAX_PAYLOAD];
};

inline ssize_t send_all(int sockfd, const void* buf, size_t len) {
    size_t total = 0;
    const char* ptr = static_cast<const char*>(buf);

    while (total < len) {
        ssize_t sent = send(sockfd, ptr + total, len - total, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (sent == 0) {
            return -1;
        }
        total += static_cast<size_t>(sent);
    }

    return static_cast<ssize_t>(total);
}

inline ssize_t recv_all(int sockfd, void* buf, size_t len) {
    size_t total = 0;
    char* ptr = static_cast<char*>(buf);

    while (total < len) {
        ssize_t received = recv(sockfd, ptr + total, len - total, 0);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (received == 0) {
            return 0;
        }
        total += static_cast<size_t>(received);
    }

    return static_cast<ssize_t>(total);
}

inline int send_message(int sockfd, uint8_t type, const std::string& payload) {
    Message msg{};
    std::memset(&msg, 0, sizeof(msg));

    size_t payload_len = payload.size();
    if (payload_len >= MAX_PAYLOAD) {
        payload_len = MAX_PAYLOAD - 1;
    }

    msg.type = type;
    std::memcpy(msg.payload, payload.c_str(), payload_len);
    msg.payload[payload_len] = '\0';
    msg.length = htonl(static_cast<uint32_t>(1 + payload_len));

    if (send_all(sockfd, &msg.length, sizeof(msg.length)) <= 0) {
        return -1;
    }
    if (send_all(sockfd, &msg.type, sizeof(msg.type)) <= 0) {
        return -1;
    }
    if (payload_len > 0) {
        if (send_all(sockfd, msg.payload, payload_len) <= 0) {
            return -1;
        }
    }

    return 0;
}

inline int recv_message(int sockfd, Message& msg) {
    std::memset(&msg, 0, sizeof(msg));

    uint32_t net_length = 0;
    ssize_t n = recv_all(sockfd, &net_length, sizeof(net_length));
    if (n <= 0) {
        return -1;
    }

    msg.length = ntohl(net_length);
    if (msg.length < 1 || msg.length > static_cast<uint32_t>(MAX_PAYLOAD)) {
        return -1;
    }

    n = recv_all(sockfd, &msg.type, sizeof(msg.type));
    if (n <= 0) {
        return -1;
    }

    size_t payload_len = msg.length - 1;
    if (payload_len > 0) {
        n = recv_all(sockfd, msg.payload, payload_len);
        if (n <= 0) {
            return -1;
        }
        msg.payload[payload_len] = '\0';
    } else {
        msg.payload[0] = '\0';
    }

    return 0;
}

inline std::string trim_newline(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

inline std::string get_peer_address(int sockfd) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);

    if (getpeername(sockfd, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
        char ip[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
    }

    return "unknown";
}

#endif