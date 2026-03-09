#pragma once

#include "protocol.h"
#include "net_utils.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cstring>
#include <string>

inline Message make_message(uint8_t type, const std::string& payload) {
    Message m{};
    m.type = type;

    size_t payload_used = std::min(payload.size(), static_cast<size_t>(MAX_PAYLOAD));
    std::memcpy(m.payload, payload.data(), payload_used);

    uint32_t len = static_cast<uint32_t>(1 + payload_used);
    m.length = htonl(len);

    return m;
}

inline void send_message(int fd, uint8_t type, const std::string& payload) {
    size_t payload_used = std::min(payload.size(), static_cast<size_t>(MAX_PAYLOAD));
    Message m = make_message(type, payload);

    writen(fd, &m.length, sizeof(m.length));
    writen(fd, &m.type, sizeof(m.type));
    if (payload_used > 0) {
        writen(fd, m.payload, payload_used);
    }
}

inline bool recv_message(int fd, uint8_t& type, std::string& payload_out) {
    uint32_t net_len = 0;
    if (!readn(fd, &net_len, sizeof(net_len))) return false;

    uint32_t len = ntohl(net_len);
    if (len < 1) throw std::runtime_error("Invalid message length (<1)");

    uint8_t t = 0;
    if (!readn(fd, &t, sizeof(t))) return false;

    uint32_t payload_used = len - 1;
    if (payload_used > MAX_PAYLOAD) {
        throw std::runtime_error("Payload too large (>MAX_PAYLOAD)");
    }

    char buf[MAX_PAYLOAD];
    if (payload_used > 0) {
        if (!readn(fd, buf, payload_used)) return false;
        payload_out.assign(buf, buf + payload_used);
    } else {
        payload_out.clear();
    }

    type = t;
    return true;
}