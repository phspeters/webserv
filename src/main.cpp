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

    // Initialize server manager
    ServerManager serverManager;
    if (!serverManager.init()) {
        return EXIT_FAILURE;
    }

    // Parse configuration file
    std::vector<ServerConfig> configs =
        serverManager.parse_config_file(config_file);
    if (configs.empty()) {
        return EXIT_FAILURE;
    }

    // Spin up servers and start event loop
    serverManager.run(configs);

    return EXIT_SUCCESS;
}