#include "common.hpp"

struct ClientInfo {
    int sockfd = -1;
    std::string nickname;
    std::string address;
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

int add_client(int sockfd, const std::string& nickname, const std::string& address) {
    std::lock_guard<std::mutex> lock(g_clients_mutex);

    for (int i = 0; i < static_cast<int>(g_clients.size()); ++i) {
        if (!g_clients[i].active) {
            g_clients[i].sockfd = sockfd;
            g_clients[i].nickname = nickname;
            g_clients[i].address = address;
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
            client.sockfd = -1;
            client.nickname.clear();
            client.address.clear();
            return;
        }
    }
}

void broadcast_message(const std::string& text) {
    std::lock_guard<std::mutex> lock(g_clients_mutex);

    for (const auto& client : g_clients) {
        if (client.active) {
            send_message(client.sockfd, MSG_TEXT, text);
        }
    }
}

void handle_client(int client_sock) {
    Message msg{};
    std::string address = get_peer_address(client_sock);
    std::string nickname;

    if (recv_message(client_sock, msg) < 0 || msg.type != MSG_HELLO) {
        close(client_sock);
        return;
    }

    nickname = std::string(msg.payload);
    if (nickname.empty()) {
        nickname = "Anonymous";
    }

    if (send_message(client_sock, MSG_WELCOME, "Welcome to the chat server!") < 0) {
        close(client_sock);
        return;
    }

    if (add_client(client_sock, nickname, address) < 0) {
        send_message(client_sock, MSG_TEXT, "Server is full");
        close(client_sock);
        return;
    }

    std::string connect_msg = "Client connected: " + nickname + " [" + address + "]";
    std::cout << connect_msg << std::endl;
    broadcast_message(connect_msg);

    while (true) {
        if (recv_message(client_sock, msg) < 0) {
            std::string disconnect_msg = "Client disconnected: " + nickname + " [" + address + "]";
            std::cout << disconnect_msg << std::endl;
            remove_client(client_sock);
            close(client_sock);
            broadcast_message(disconnect_msg);
            return;
        }

        switch (msg.type) {
            case MSG_TEXT: {
                std::string text = nickname + " [" + address + "]: " + std::string(msg.payload);
                std::cout << text << std::endl;
                broadcast_message(text);
                break;
            }

            case MSG_PING:
                if (send_message(client_sock, MSG_PONG, "PONG") < 0) {
                    std::string disconnect_msg = "Client disconnected: " + nickname + " [" + address + "]";
                    std::cout << disconnect_msg << std::endl;
                    remove_client(client_sock);
                    close(client_sock);
                    broadcast_message(disconnect_msg);
                    return;
                }
                break;

            case MSG_BYE: {
                std::string disconnect_msg = "Client disconnected: " + nickname + " [" + address + "]";
                std::cout << disconnect_msg << std::endl;
                remove_client(client_sock);
                close(client_sock);
                broadcast_message(disconnect_msg);
                return;
            }

            default:
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