#include "common.hpp"

int main(int argc, char* argv[]) {
    // Check if the user wants to validate the configuration file only
    if (argc == 3 && std::string(argv[2]) == "--validate-only") {
        std::string config_file = argv[1];
        WebServer web_server;
        if (web_server.parse_config_file(config_file)) {
            std::cout << "Configuration is valid." << std::endl;
            return EXIT_SUCCESS;
        } else {
            return EXIT_FAILURE;
        }
    }

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