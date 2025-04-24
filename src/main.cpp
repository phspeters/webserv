#include "webserv.hpp"

int main(int argc, char* argv[]) {
    // Ensure the configuration file is provided as a command-line argument
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <server.conf>" << std::endl;
        return EXIT_FAILURE;
    }

    std::string config_file = argv[1];
    ServerConfig serverConfig;

    // Load the configuration file
    if (!serverConfig.loadFromFile(config_file)) {
        std::cerr << "Error: Failed to load configuration from file: "
                  << config_file << std::endl;
        return EXIT_FAILURE;
    }

    // Print a summary of the loaded configuration
    std::cout << "Configuration loaded successfully from: " << config_file
              << std::endl;
    std::cout << "Server count: " << serverConfig.servers.size() << std::endl;

    for (size_t i = 0; i < serverConfig.servers.size(); ++i) {
        const ServerBlock& server = serverConfig.servers[i];
        std::cout << "Server " << i + 1 << ":" << std::endl;
        std::cout << "  Host: " << server.host << std::endl;
        std::cout << "  Port: " << server.port << std::endl;
        std::cout << "  Max Body Size: " << server.max_body_size << " bytes"
                  << std::endl;

        for (size_t j = 0; j < server.routes.size(); ++j) {
            const RouteConfig& route = server.routes[j];
            std::cout << "  Route " << j + 1 << ":" << std::endl;
            std::cout << "    Path: " << route.path << std::endl;
            std::cout << "    Root: " << route.root << std::endl;
            std::cout << "    Index File: " << route.index_file << std::endl;
            std::cout << "    Directory Listing: "
                      << (route.directory_listing ? "Enabled" : "Disabled")
                      << std::endl;
            std::cout << "    Allowed Methods: ";
            for (std::vector<std::string>::const_iterator it =
                     route.allowed_methods.begin();
                 it != route.allowed_methods.end(); ++it) {
                std::cout << *it << " ";
            }
            std::cout << std::endl;
        }
    }

    std::cout << "Server is ready to start (not implemented)." << std::endl;

    return EXIT_SUCCESS;
}
/*
Core Server & Management: Server.hpp, ServerConfig.hpp, ConnectionManager.hpp,
Connection.hpp define the main server loop, configuration handling, connection
lifecycle, and individual connection state. HTTP Handling: Request.hpp,
Response.hpp, RequestParser.hpp, ResponseWriter.hpp define how HTTP requests and
responses are represented, parsed, and formatted. Request Processing Logic:
IHandler.hpp (base class), StaticFileHandler.hpp, CgiHandler.hpp, and Router.hpp
define the request handling abstraction, specific handlers for static files and
CGI, and the mechanism to choose the correct handler.
*/