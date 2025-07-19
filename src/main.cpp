#include "common.hpp"

int main(int argc, char* argv[]) {
    // Ensure the configuration file is provided as a command-line argument

    if (argc > 2) {
        std::cerr << "Usage: " << argv[0] << " <server.conf>" << std::endl;
        return EXIT_FAILURE;
    }

    std::string config_file = "config/unified_server.conf";
    if (argc == 2) {
        config_file = argv[1];
    }

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