#include "common.hpp"
#include <atomic>

class ClientApp {
public:
    ClientApp(const std::string& ip, int port, const std::string& nickname)
        : server_ip_(ip), port_(port), nickname_(nickname) {
        std::signal(SIGPIPE, SIG_IGN);
    }

    void run() {
        if (!connect_and_handshake()) {
            std::cout << "Initial connection failed, will try to reconnect automatically" << std::endl;
        }

        receiver_thread_ = std::thread(&ClientApp::receiver_loop, this);

        std::string input;
        while (running_ && std::getline(std::cin, input)) {
            input = trim_newline(input);

            if (input == "/quit") {
                int sock = get_socket();
                if (is_connected_) {
                    send_message(sock, MSG_BYE, "bye");
                }
                running_ = false;
                close_connection();
                break;
            }

            if (input == "/ping") {
                if (is_connected_) {
                    int sock = get_socket();
                    if (send_message(sock, MSG_PING, "ping") < 0) {
                        std::cout << "Failed to send PING" << std::endl;
                    }
                } else {
                    std::cout << "Not connected to server" << std::endl;
                }
                continue;
            }

            if (!input.empty()) {
                if (is_connected_) {
                    int sock = get_socket();
                    if (send_message(sock, MSG_TEXT, input) < 0) {
                        std::cout << "Failed to send message" << std::endl;
                    }
                } else {
                    std::cout << "Not connected to server" << std::endl;
                }
            }
        }

        running_ = false;
        close_connection();

        if (receiver_thread_.joinable()) {
            receiver_thread_.join();
        }
    }

private:
    bool connect_and_handshake() {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            return false;
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port_);

        if (inet_pton(AF_INET, server_ip_.c_str(), &server_addr.sin_addr) <= 0) {
            close(sockfd);
            return false;
        }

        if (connect(sockfd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
            close(sockfd);
            return false;
        }

        if (send_message(sockfd, MSG_HELLO, nickname_) < 0) {
            close(sockfd);
            return false;
        }

        Message msg{};
        if (recv_message(sockfd, msg) < 0 || msg.type != MSG_WELCOME) {
            close(sockfd);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            sockfd_ = sockfd;
            is_connected_ = true;
        }

        std::cout << "[server] " << msg.payload << std::endl;
        return true;
    }

    void close_connection() {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (sockfd_ >= 0) {
            close(sockfd_);
            sockfd_ = -1;
        }
        is_connected_ = false;
    }

    int get_socket() {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        return sockfd_;
    }

    void reconnect_loop() {
        while (running_ && !is_connected_) {
            std::cout << "Trying to reconnect in " << RECONNECT_DELAY_SEC << " seconds..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(RECONNECT_DELAY_SEC));

            if (!running_) {
                return;
            }

            if (connect_and_handshake()) {
                std::cout << "Reconnected successfully" << std::endl;
                return;
            }

            std::cout << "Reconnect failed" << std::endl;
        }
    }

    void receiver_loop() {
        while (running_) {
            if (!is_connected_) {
                reconnect_loop();
                continue;
            }

            int sock = get_socket();
            Message msg{};

            if (recv_message(sock, msg) < 0) {
                std::cout << "Connection lost" << std::endl;
                close_connection();
                continue;
            }

            switch (msg.type) {
                case MSG_TEXT:
                    std::cout << msg.payload << std::endl;
                    break;
                case MSG_PONG:
                    std::cout << "[server] PONG" << std::endl;
                    break;
                case MSG_WELCOME:
                    std::cout << "[server] " << msg.payload << std::endl;
                    break;
                default:
                    break;
            }
        }
    }

private:
    std::string server_ip_;
    int port_;
    std::string nickname_;

    int sockfd_ = -1;
    std::atomic<bool> running_{true};
    std::atomic<bool> is_connected_{false};

    std::mutex socket_mutex_;
    std::thread receiver_thread_;
};

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <server_ip> <port> <nickname>" << std::endl;
        return 1;
    }

    std::string ip = argv[1];
    int port = std::atoi(argv[2]);
    std::string nickname = argv[3];

    ClientApp app(ip, port, nickname);
    app.run();

    return 0;
}