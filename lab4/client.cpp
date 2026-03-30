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
            std::cout << "Initial connection failed, reconnect will be attempted automatically" << std::endl;
        }

        receiver_thread_ = std::thread(&ClientApp::receiver_loop, this);

        std::string input;
        while (running_ && std::getline(std::cin, input)) {
            input = trim_newline(input);

            if (input == "/quit") {
                int sock = get_socket();
                if (is_connected_) {
                    send_message_raw(sock, MSG_BYE, "bye");
                }
                running_ = false;
                close_connection();
                break;
            }

            if (input == "/ping") {
                if (is_connected_) {
                    int sock = get_socket();
                    if (send_message_raw(sock, MSG_PING, "ping") < 0) {
                        std::cout << "Failed to send PING" << std::endl;
                    }
                } else {
                    std::cout << "Not connected to server" << std::endl;
                }
                continue;
            }

            if (input.rfind("/w ", 0) == 0) {
                handle_private_command(input);
                continue;
            }

            if (!input.empty()) {
                if (is_connected_) {
                    int sock = get_socket();
                    if (send_message_raw(sock, MSG_TEXT, input) < 0) {
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

        if (send_message_raw(sockfd, MSG_HELLO, "HELLO") < 0) {
            close(sockfd);
            return false;
        }

        Message msg{};
        if (recv_message(sockfd, msg) < 0 || msg.type != MSG_WELCOME) {
            close(sockfd);
            return false;
        }

        if (send_message_raw(sockfd, MSG_AUTH, nickname_) < 0) {
            close(sockfd);
            return false;
        }

        if (recv_message(sockfd, msg) < 0) {
            close(sockfd);
            return false;
        }

        if (msg.type == MSG_ERROR) {
            std::cout << "[SERVER]: " << msg.payload << std::endl;
            close(sockfd);
            return false;
        }

        if (msg.type != MSG_SERVER_INFO) {
            close(sockfd);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            sockfd_ = sockfd;
            is_connected_ = true;
        }

        std::cout << "[SERVER]: " << msg.payload << std::endl;
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
                case MSG_PRIVATE:
                    std::cout << msg.payload << std::endl;
                    break;
                case MSG_SERVER_INFO:
                    std::cout << "[SERVER]: " << msg.payload << std::endl;
                    break;
                case MSG_ERROR:
                    std::cout << "[SERVER][ERROR]: " << msg.payload << std::endl;
                    break;
                case MSG_PONG:
                    std::cout << "[SERVER]: PONG" << std::endl;
                    break;
                default:
                    break;
            }
        }
    }

    void handle_private_command(const std::string& input) {
        if (!is_connected_) {
            std::cout << "Not connected to server" << std::endl;
            return;
        }

        size_t first_space = input.find(' ');
        if (first_space == std::string::npos) {
            std::cout << "Usage: /w <nick> <message>" << std::endl;
            return;
        }

        size_t second_space = input.find(' ', first_space + 1);
        if (second_space == std::string::npos) {
            std::cout << "Usage: /w <nick> <message>" << std::endl;
            return;
        }

        std::string target = input.substr(first_space + 1, second_space - first_space - 1);
        std::string message = input.substr(second_space + 1);

        if (target.empty() || message.empty()) {
            std::cout << "Usage: /w <nick> <message>" << std::endl;
            return;
        }

        std::string payload = target + ":" + message;
        int sock = get_socket();

        if (send_message_raw(sock, MSG_PRIVATE, payload) < 0) {
            std::cout << "Failed to send private message" << std::endl;
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

    if (nickname.empty()) {
        std::cerr << "Nickname cannot be empty" << std::endl;
        return 1;
    }

    ClientApp app(ip, port, nickname);
    app.run();

    return 0;
}