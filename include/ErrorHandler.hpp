#ifndef ERRORHANDLER_HPP
#define ERRORHANDLER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
struct HttpResponse;
struct ServerConfig;

// Centralized error handling for HTTP responses
namespace ErrorHandler {
    // Main error handling method - applies error to the response
    void handle_error(HttpResponse* resp, int status_code, const ServerConfig& config);
    
    // Convenience methods for common HTTP errors
    void bad_request(HttpResponse* resp, const ServerConfig& config);
    void unauthorized(HttpResponse* resp, const ServerConfig& config);
    void forbidden(HttpResponse* resp, const ServerConfig& config);
    void not_found(HttpResponse* resp, const ServerConfig& config);
    void method_not_allowed(HttpResponse* resp, const ServerConfig& config);
    void payload_too_large(HttpResponse* resp, const ServerConfig& config);
    void unsupported_media_type(HttpResponse* resp, const ServerConfig& config);
    void internal_server_error(HttpResponse* resp, const ServerConfig& config);
    void not_implemented(HttpResponse* resp, const ServerConfig& config);
    void request_timeout(HttpResponse* resp, const ServerConfig& config);
    void too_many_requests(HttpResponse* resp, const ServerConfig& config, int retry_after = 60);
    
    // Apply error response to connection
    void apply_to_connection(Connection* conn, int status_code, const ServerConfig& config);
    
    // Helper functions
    std::string get_error_page_content(int status_code, const ServerConfig& config);
    std::string generate_default_error_page(int status_code, const std::string& status_message);
}

#endif // ERRORHANDLER_HPP