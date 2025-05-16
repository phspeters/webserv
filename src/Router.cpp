#include "webserv.hpp"

Router::Router(const ServerConfig& config, StaticFileHandler* static_handler,
               CgiHandler* cgi_handler, FileUploadHandler* file_upload_handler)
    : config_(config),
      static_handler_(static_handler),
      cgi_handler_(cgi_handler),
      file_upload_handler_(file_upload_handler) {}

Router::~Router() {
    // Clean up owned components
}

AHandler* Router::route(HttpRequest* req) {
    
    // Get the path from the request (already without query part)
    const std::string& request_path = req->uri_;
    const std::string& request_method = req->method_;

    // Iterate through locations in config_ to find a match
    const LocationConfig* matching_location = NULL;

    for (std::vector<LocationConfig>::const_iterator it = config_.locations.begin(); it != config_.locations.end(); ++it) {
        const LocationConfig& location = *it;
        // Check if the request path starts with the location path
        if (request_path.find(location.path) == 0) {
            // Make sure we match complete segments
            if (location.path == "/" ||           // Root always matches
                request_path == location.path ||  // Exact match
                (request_path.length() > location.path.length() && (request_path[location.path.length()] == '/' || request_path[location.path.length() - 1] == '/'))) { 
                if (!matching_location || location.path.length() > matching_location->path.length()) { 
                    matching_location = &location;
                }
            }
        }
    }

    req->location_match_ = matching_location;

    // Print router info when a location is matched
    std::cout << "\n==== ROUTER INFO ====\n";
    std::cout << "Request path: " << request_path << std::endl;
    std::cout << "Matching location: " << matching_location->path << std::endl;
    std::cout << "Autoindex: "
              << (matching_location->autoindex ? "true" : "false") << std::endl;
    std::cout << "CGI enabled: "
              << (matching_location->cgi_enabled ? "true" : "false")
              << std::endl;
    std::cout << "Root: " << matching_location->root << std::endl;
    std::cout << "======================\n" << std::endl;

    // Return appropriate handler based on location config
    if (matching_location->cgi_enabled) {
        // CGI handler for CGI-enabled locations
        return cgi_handler_;
    } else if (request_method == "POST" || request_method == "DELETE") {
        // FileUploadHandler for file uploads
        return file_upload_handler_;
    } else {
        // Default to StaticFileHandler for regular files
        return static_handler_;
    }
}