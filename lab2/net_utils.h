#pragma once

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

inline bool readn(int fd, void* buf, size_t n) {
    uint8_t* p = static_cast<uint8_t*>(buf);
    size_t left = n;

    while (left > 0) {
        ssize_t r = ::recv(fd, p, left, 0);
        if (r == 0) return false;
        if (r < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error(std::string("recv() failed: ") + std::strerror(errno));
        }
        p += static_cast<size_t>(r);
        left -= static_cast<size_t>(r);
    }
    return true;
}

inline void writen(int fd, const void* buf, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(buf);
    size_t left = n;

    while (left > 0) {
        ssize_t w = ::send(fd, p, left, 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error(std::string("send() failed: ") + std::strerror(errno));
        }
        p += static_cast<size_t>(w);
        left -= static_cast<size_t>(w);
    }
}