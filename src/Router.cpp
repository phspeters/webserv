#include "webserv.hpp"

Router::Router(const ServerConfig& config) : config_(config) {}

Router::~Router() {
    // Clean up owned components
}

IHandler* Router::route(const HttpRequest* req) {

    // Get the path from the request (already without query part) --- CHECK req->uri
    const std::string& request_path = req->uri_;

    // Iterate through locations in config_ to find a match
    const LocationConfig* matching_location = NULL;

    for (std::vector<LocationConfig>::const_iterator it = config_.locations.begin(); 
        it != config_.locations.end(); ++it) {
        const LocationConfig& loc = *it;
        
        // Check if the request path starts with the location path
        if (request_path.find(loc.path) == 0) {
            // Make sure we match complete segments
            // For example, /blog should match / but not match /blogs
            if (loc.path == "/" || // Root always matches
                request_path == loc.path || // Exact match
                (request_path.length() > loc.path.length() && request_path[loc.path.length()] == '/')) {
                // If we haven't found a match yet, or this match is more specific
                if (!matching_location || loc.path.length() > matching_location->path.length()) {
                    matching_location = &loc;
                }
            }
        }
    }

    // If no matching location found, send to StaticFileHandler? CHECK
    if (!matching_location) {
        std::cout << "\n==== ROUTER INFO ====\n";
        std::cout << "Request path: " << request_path << std::endl;
        std::cout << "Matching location: NONE" << std::endl;
        std::cout << "======================\n" << std::endl;
        return NULL;
    }

    // Print router info when a location is matched
    std::cout << "\n==== ROUTER INFO ====\n";
    std::cout << "Request path: " << request_path << std::endl;
    std::cout << "Matching location: " << matching_location->path << std::endl;
    std::cout << "Autoindex: " << (matching_location->autoindex ? "true" : "false") << std::endl;
    std::cout << "CGI enabled: " << (matching_location->cgi_enabled ? "true" : "false") << std::endl;
    std::cout << "Root: " << matching_location->root << std::endl;
    std::cout << "======================\n" << std::endl;

    // Determine which handler to use based on the matching location's configuration
    ResponseWriter* writer = new ResponseWriter(config_);
    if (matching_location->cgi_enabled) {
        // return new CgiHandler(config_, *writer); // 
        return new StaticFileHandler(config_, *writer);   
    } else if (matching_location->autoindex) {
        return new FileUploadHandler(config_, *writer); 
    } else {
        return new StaticFileHandler(config_, *writer);
    }
}
