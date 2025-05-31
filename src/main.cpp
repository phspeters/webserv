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


std::string get_status_message(int code) {
    switch (code) {
        // 2xx - Success
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 206:
            return "Partial Content";

        // 3xx - Redirection
        case 301:
            return "Moved Permanently";
        case 302:
            return "Found";
        case 303:
            return "See Other";
        case 304:
            return "Not Modified";
        case 307:
            return "Temporary Redirect";
        case 308:
            return "Permanent Redirect";

        // 4xx - Client Error
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 406:
            return "Not Acceptable";
        case 408:
            return "Request Timeout";
        case 409:
            return "Conflict";
        case 413:
            return "Payload Too Large";
        case 414:
            return "URI Too Long";
        case 415:
            return "Unsupported Media Type";
        case 429:
            return "Too Many Requests";
        case 431:
            return "Request Header Fields Too Large";

        // 5xx - Server Error
        case 500:
            return "Internal Server Error";
        case 501:
            return "Not Implemented";
        case 502:
            return "Bad Gateway";
        case 503:
            return "Service Unavailable";
        case 504:
            return "Gateway Timeout";
        case 505:
            return "HTTP Version Not Supported";

        default:
            return "Unknown Status";
    }
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