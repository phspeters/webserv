#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#define CHUNK_SIZE 4096

class SimpleHttpClient {
   private:
    int sockfd_;
    struct sockaddr_in server_addr_;

   public:
    SimpleHttpClient() : sockfd_(-1) {}

    ~SimpleHttpClient() {
        if (sockfd_ >= 0) {
            close(sockfd_);
        }
    }

    bool connect(const std::string& host, int port) {
        // Create socket
        sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd_ < 0) {
            std::cerr << "Error creating socket\n";
            return false;
        }

        // Setup server address
        memset(&server_addr_, 0, sizeof(server_addr_));
        server_addr_.sin_family = AF_INET;
        server_addr_.sin_port = htons(port);

        // Try to convert as IP address first
        if (inet_pton(AF_INET, host.c_str(), &server_addr_.sin_addr) <= 0) {
            // Not a valid IP address, try to resolve hostname
            struct hostent* he;
            if ((he = gethostbyname(host.c_str())) == NULL) {
                std::cerr << "Could not resolve hostname: " << host
                          << std::endl;
                return false;
            }
            // Copy the first IP address
            memcpy(&server_addr_.sin_addr, he->h_addr_list[0], he->h_length);
        }

        // Connect to server
        if (::connect(sockfd_, (struct sockaddr*)&server_addr_,
                      sizeof(server_addr_)) < 0) {
            std::cerr << "Connection failed to " << host << ":" << port
                      << std::endl;
            return false;
        }

        std::cout << "Connected to " << host << ":" << port << std::endl;
        return true;
    }

    bool send_request(const std::string& request) {
        if (write(sockfd_, request.c_str(), request.length()) !=
            (ssize_t)request.length()) {
            std::cerr << "Failed to send complete request\n";
            return false;
        }
        return true;
    }

    std::string receive_response() {
        char buffer[CHUNK_SIZE];
        std::string response;
        ssize_t bytes_read;

        while ((bytes_read = read(sockfd_, buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0';
            response += buffer;

            if (bytes_read < (ssize_t)sizeof(buffer) - 1) {
                break;
            }
        }

        if (bytes_read < 0) {
            std::cerr << "Error reading response\n";
        }

        return response;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <host> <port> [path]\n";
        return 1;
    }

    std::string host = argv[1];
    int port = std::atoi(argv[2]);
    std::string path = argc > 3 ? argv[3] : "/";

    SimpleHttpClient client;

    if (!client.connect(host, port)) {
        return 1;
    }

    // Create a simple HTTP request
    std::string request = "GET " + path +
                          " HTTP/1.1\r\n"
                          "Host: " +
                          host +
                          "\r\n"
                          "Connection: keep-alive\r\n"
                          "\r\n";

    std::cout << "Sending request:\n" << request << std::endl;

    if (!client.send_request(request)) {
        return 1;
    }

    std::string response = client.receive_response();
    std::cout << "Received response:\n" << response << std::endl;

    // Option to add another request to test keep-alive
    std::cout << "Press Enter to send another request or Ctrl+C to exit..."
              << std::endl;
    std::cin.get();

    request =
        "GET /another-path HTTP/1.1\r\n"
        "Host: " +
        host +
        "\r\n"
        "Connection: close\r\n"
        "\r\n";

    std::cout << "Sending second request:\n" << request << std::endl;

    if (!client.send_request(request)) {
        return 1;
    }

    response = client.receive_response();
    std::cout << "Received response to second request:\n"
              << response << std::endl;

    return 0;
}