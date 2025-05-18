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

    // Use centralized location matching from config
    const LocationConfig* matching_location = config_.findMatchingLocation(request_path);
    req->location_match_ = matching_location;

    // Check if a matching location was found
    if (!matching_location) {
        std::cout << "\n==== ROUTER ERROR ====\n";
        std::cout << "No matching location for path: " << request_path << std::endl;
        std::cout << "======================\n" << std::endl;

        ErrorHandler::not_found(req->response_data_, config_);
        // Return default handler or handle error case
        // return static_handler_; // Usually you'd return an error handler instead
    }

     // Check if the requested method is allowed for this location
     if (!matching_location->allowed_methods.empty()) {
        bool method_allowed = false;
        std::string allowed_methods_str;
        
        for (size_t i = 0; i < matching_location->allowed_methods.size(); i++) {
            // Build Allow header value
            if (i > 0) {
                allowed_methods_str += ", ";
            }
            allowed_methods_str += matching_location->allowed_methods[i];
            
            // Check if the current request method is allowed
            if (matching_location->allowed_methods[i] == request_method) {
                method_allowed = true;
            }
        }
        
        if (!method_allowed) {
            std::cout << "\n==== ROUTER ERROR ====\n";
            std::cout << "Method not allowed: " << request_method << std::endl;
            std::cout << "Allowed methods: " << allowed_methods_str << std::endl;
            std::cout << "======================\n" << std::endl;
            
            // Apply 405 error directly to the response
            ErrorHandler::method_not_allowed(req->response_data_, config_);
            
            // Add the Allow header
            req->response_data_->headers_["Allow"] = allowed_methods_str;
            
            // return static_handler_; // Return static handler but response is already set
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