#include "webserv.hpp"

// Helper function to trim whitespace
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, last - first + 1);
}

int main(int argc, char* argv[]) {
    // Ensure the configuration file is provided as a command-line argument
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <server.conf>" << std::endl;
        return EXIT_FAILURE;
    }

    std::string config_file = argv[1];
    WebServer web_server;

    // Parse configuration file
    if (!web_server.parse_config_file(config_file)) {
        return EXIT_FAILURE;
    }

    // Initialize server manager
    if (!web_server.init()) {
        return EXIT_FAILURE;
    }

    // Spin up server and start event loop
    web_server.run();

    return EXIT_SUCCESS;
}