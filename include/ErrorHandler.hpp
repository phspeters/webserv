#ifndef ERRORHANDLER_HPP
#define ERRORHANDLER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
struct HttpResponse;
struct VirtualServer;

// Centralized error handling for HTTP responses
class ErrorHandler {
public:
    // Constructor
    ErrorHandler();
    ~ErrorHandler();
    
    // Main error handling method - applies error to the response
    void handle_error(HttpResponse* resp, int status_code);
    
    // Convenience methods for common HTTP errors
    void bad_request(HttpResponse* resp);
    void unauthorized(HttpResponse* resp);
    void forbidden(HttpResponse* resp);
    void not_found(HttpResponse* resp);
    void method_not_allowed(HttpResponse* resp);
    void payload_too_large(HttpResponse* resp);
    void unsupported_media_type(HttpResponse* resp);
    void internal_server_error(HttpResponse* resp);
    void not_implemented(HttpResponse* resp);
    
    // Apply error response to connection
    void apply_to_connection(Connection* conn, int status_code);
    
private:
    // Get error page content for the given status code
    std::string get_error_page_content(int status_code, const VirtualServer* virtual_server = NULL);
    
    // Generate default error page when custom one doesn't exist
    std::string generate_default_error_page(int status_code, const std::string& status_message);
    
    // Get status message for a status code
    std::string get_status_message(int code) const;
    
    // Prevent copying
    ErrorHandler(const ErrorHandler&);
    ErrorHandler& operator=(const ErrorHandler&);
};

#endif // ERRORHANDLER_HPP