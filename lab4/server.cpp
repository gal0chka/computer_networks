#include "common.hpp"
#include <sstream>

struct ClientInfo {
    int sockfd = -1;
    std::string nickname;
    std::string address;
    bool authenticated = false;
    bool active = false;
};

class SocketQueue {
public:
    void push(int sockfd) {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.push(sockfd);
        cond_.notify_one();
    }

    int pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this]() { return !queue_.empty(); });

        int sockfd = queue_.front();
        queue_.pop();
        return sockfd;
    }

private:
    std::queue<int> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

SocketQueue g_socket_queue;
std::vector<ClientInfo> g_clients(MAX_CLIENTS);
std::mutex g_clients_mutex;

bool nickname_exists(const std::string& nickname) {
    for (const auto& client : g_clients) {
        if (client.active && client.authenticated && client.nickname == nickname) {
            return true;
        }
    }
    return false;
}

int add_client(int sockfd, const std::string& nickname, const std::string& address) {
    std::lock_guard<std::mutex> lock(g_clients_mutex);

    if (nickname_exists(nickname)) {
        return -2;
    }

    for (int i = 0; i < static_cast<int>(g_clients.size()); ++i) {
        if (!g_clients[i].active) {
            g_clients[i].sockfd = sockfd;
            g_clients[i].nickname = nickname;
            g_clients[i].address = address;
            g_clients[i].authenticated = true;
            g_clients[i].active = true;
            return i;
        }
    }

    return -1;
}

void remove_client(int sockfd) {
    std::lock_guard<std::mutex> lock(g_clients_mutex);

    for (auto& client : g_clients) {
        if (client.active && client.sockfd == sockfd) {
            client.active = false;
            client.authenticated = false;
            client.sockfd = -1;
            client.nickname.clear();
            client.address.clear();
            return;
        }
    }
}

ClientInfo* find_client_by_nick(const std::string& nickname) {
    for (auto& client : g_clients) {
        if (client.active && client.authenticated && client.nickname == nickname) {
            return &client;
        }
    }
    return nullptr;
}

void send_server_info_to_all(const std::string& text) {
    std::lock_guard<std::mutex> lock(g_clients_mutex);

    for (const auto& client : g_clients) {
        if (client.active && client.authenticated) {
            send_message(client.sockfd, MSG_SERVER_INFO, text);
        }
    }
}

void broadcast_text(const std::string& text) {
    std::lock_guard<std::mutex> lock(g_clients_mutex);

    for (const auto& client : g_clients) {
        if (client.active && client.authenticated) {
            send_message(client.sockfd, MSG_TEXT, text);
        }
    }
}

bool send_private_message(const std::string& target_nick, const std::string& text) {
    std::lock_guard<std::mutex> lock(g_clients_mutex);

    ClientInfo* target = find_client_by_nick(target_nick);
    if (target == nullptr) {
        return false;
    }

    return send_message(target->sockfd, MSG_PRIVATE, text) == 0;
}

bool perform_initial_handshake(int sockfd) {
    Message msg{};

    if (recv_message(sockfd, msg) < 0 || msg.type != MSG_HELLO) {
        return false;
    }

    osi_session_log("initial HELLO received");
    if (send_message(sockfd, MSG_WELCOME, "Welcome") < 0) {
        return false;
    }

    osi_session_log("initial WELCOME sent");
    return true;
}

bool authenticate_client(int sockfd, std::string& nickname_out, const std::string& address) {
    Message msg{};

    if (recv_message(sockfd, msg) < 0) {
        return false;
    }

    if (msg.type != MSG_AUTH) {
        osi_session_log("authentication failed: expected MSG_AUTH");
        send_message(sockfd, MSG_ERROR, "Authentication required");
        return false;
    }

    std::string nickname = msg.payload;
    osi_app_log("handle MSG_AUTH");

    if (nickname.empty()) {
        osi_session_log("authentication failed: empty nickname");
        send_message(sockfd, MSG_ERROR, "Nickname cannot be empty");
        return false;
    }

    int result = add_client(sockfd, nickname, address);
    if (result == -2) {
        osi_session_log("authentication failed: nickname already exists");
        send_message(sockfd, MSG_ERROR, "Nickname already in use");
        return false;
    }
    if (result == -1) {
        osi_session_log("authentication failed: server full");
        send_message(sockfd, MSG_ERROR, "Server is full");
        return false;
    }

    nickname_out = nickname;
    osi_session_log("authentication success");
    send_message(sockfd, MSG_SERVER_INFO, "Authentication successful");
    return true;
}

bool parse_private_payload(const std::string& payload, std::string& target, std::string& text) {
    size_t pos = payload.find(':');
    if (pos == std::string::npos) {
        return false;
    }

    target = payload.substr(0, pos);
    text = payload.substr(pos + 1);

    if (target.empty() || text.empty()) {
        return false;
    }

    return true;
}

void handle_client(int client_sock) {
    std::string address = get_peer_address(client_sock);
    std::string nickname;

    std::cout << "Client connected" << std::endl;

    if (!perform_initial_handshake(client_sock)) {
        close(client_sock);
        return;
    }

    if (!authenticate_client(client_sock, nickname, address)) {
        close(client_sock);
        return;
    }

    std::string connect_msg = "User [" + nickname + "] connected";
    std::cout << connect_msg << std::endl;
    send_server_info_to_all(connect_msg);

    while (true) {
        Message msg{};

        if (recv_message(client_sock, msg) < 0) {
            std::string disconnect_msg = "User [" + nickname + "] disconnected";
            std::cout << disconnect_msg << std::endl;
            remove_client(client_sock);
            close(client_sock);
            send_server_info_to_all(disconnect_msg);
            return;
        }

        osi_session_log("client authenticated");

        switch (msg.type) {
            case MSG_TEXT: {
                osi_app_log("handle MSG_TEXT");
                std::string text = "[" + nickname + "]: " + std::string(msg.payload);
                broadcast_text(text);
                break;
            }

            case MSG_PRIVATE: {
                osi_app_log("handle MSG_PRIVATE");
                std::string target_nick;
                std::string private_text;

                if (!parse_private_payload(msg.payload, target_nick, private_text)) {
                    send_message(client_sock, MSG_ERROR, "Invalid private message format. Use target:message");
                    break;
                }

                std::string final_text = "[PRIVATE][" + nickname + "]: " + private_text;
                if (!send_private_message(target_nick, final_text)) {
                    send_message(client_sock, MSG_ERROR, "Target user not found");
                }
                break;
            }

            case MSG_PING:
                osi_app_log("handle MSG_PING");
                if (send_message(client_sock, MSG_PONG, "PONG") < 0) {
                    std::string disconnect_msg = "User [" + nickname + "] disconnected";
                    std::cout << disconnect_msg << std::endl;
                    remove_client(client_sock);
                    close(client_sock);
                    send_server_info_to_all(disconnect_msg);
                    return;
                }
                break;

            case MSG_BYE: {
                osi_app_log("handle MSG_BYE");
                std::string disconnect_msg = "User [" + nickname + "] disconnected";
                std::cout << disconnect_msg << std::endl;
                remove_client(client_sock);
                close(client_sock);
                send_server_info_to_all(disconnect_msg);
                return;
            }

            default:
                osi_app_log("unsupported message for authenticated client");
                send_message(client_sock, MSG_ERROR, "Unsupported message type");
                break;
        }
    }
}

void worker_loop() {
    while (true) {
        int client_sock = g_socket_queue.pop();
        handle_client(client_sock);
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGPIPE, SIG_IGN);

    int port = DEFAULT_PORT;
    if (argc > 1) {
        port = std::atoi(argv[1]);
        if (port <= 0) {
            port = DEFAULT_PORT;
        }
    }

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_sock);
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_sock);
        return 1;
    }

    if (listen(server_sock, 16) < 0) {
        perror("listen");
        close(server_sock);
        return 1;
    }

    std::vector<std::thread> workers;
    workers.reserve(THREAD_POOL_SIZE);

    for (int i = 0; i < THREAD_POOL_SIZE; ++i) {
        workers.emplace_back(worker_loop);
        workers.back().detach();
    }

    std::cout << "Server started on port " << port << std::endl;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_sock = accept(server_sock, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_sock < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            continue;
        }

        g_socket_queue.push(client_sock);
    }

    close(server_sock);
    return 0;
}