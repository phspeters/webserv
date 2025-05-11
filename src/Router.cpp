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

    // Iterate through locations in config_ to find a match
    const LocationConfig* matching_location = NULL;

    for (std::vector<LocationConfig>::const_iterator it =
             config_.locations.begin();
         it != config_.locations.end(); ++it) {
        const LocationConfig& loc = *it;
        // Check if the request path starts with the location path
        if (request_path.find(loc.path) == 0) {
            // Make sure we match complete segments
            if (loc.path == "/" ||           // Root always matches
                request_path == loc.path ||  // Exact match
                (request_path.length() > loc.path.length() &&
                 request_path[loc.path.length()] == '/')) {
                // If we haven't found a match yet, or this match is more
                // specific
                if (!matching_location ||
                    loc.path.length() > matching_location->path.length()) {
                    matching_location = &loc;
                }
            }
        }
    }

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

    // Check if this is a directory path
    bool is_directory_request = false;
    // Path ending with / typically indicates a directory, but exclude the root
    // path (/)
    if (!request_path.empty() && request_path[request_path.size() - 1] == '/' &&
        request_path != "/") {
        is_directory_request = true;
    }

	// TEMP - Print error if no matching location found
	if (matching_location == NULL) {
		std::cerr << "No matching location found for request path: "
				  << request_path << std::endl;
		return NULL;  // No matching location found
	}

    // Return appropriate handler based on location config
    if (matching_location->cgi_enabled) {
        // CGI handler for CGI-enabled locations
        return cgi_handler_;
    } else if (is_directory_request) {
        // FileUploadHandler for file uploads
        return file_upload_handler_;
    } else {
        // Default to StaticFileHandler for regular files
        return static_handler_;
    }
}