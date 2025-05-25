#ifndef ERRORHANDLER_HPP
#define ERRORHANDLER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
struct HttpResponse;
struct VirtualServer;

// Centralized error handling for HTTP responses
namespace ErrorHandler {

void generate_error_response(Connection* conn);
void generate_error_response(Connection* conn, codes::ResponseStatus status);

// Main error handling method - applies error to the response
void handle_error(HttpResponse* resp, int status_code,
                  const VirtualServer& virtual_server);

// Convenience methods for common HTTP errors
void bad_request(HttpResponse* resp, const VirtualServer& virtual_server);
void unauthorized(HttpResponse* resp, const VirtualServer& virtual_server);
void forbidden(HttpResponse* resp, const VirtualServer& virtual_server);
void not_found(HttpResponse* resp, const VirtualServer& virtual_server);
void method_not_allowed(HttpResponse* resp,
                        const VirtualServer& virtual_server);
void payload_too_large(HttpResponse* resp, const VirtualServer& virtual_server);
void unsupported_media_type(HttpResponse* resp,
                            const VirtualServer& virtual_server);
void internal_server_error(HttpResponse* resp,
                           const VirtualServer& virtual_server);
void not_implemented(HttpResponse* resp, const VirtualServer& virtual_server);
void request_timeout(HttpResponse* resp, const VirtualServer& virtual_server);
void too_many_requests(HttpResponse* resp, const VirtualServer& virtual_server,
                       int retry_after = 60);

// Apply error response to connection
void apply_to_connection(Connection* conn, int status_code,
                         const VirtualServer& virtual_server);

// Helper functions
std::string get_error_page_content(int status_code,
                                   const VirtualServer& virtual_server);
std::string generate_default_error_page(int status_code,
                                        const std::string& status_message);
}  // namespace ErrorHandler

#endif  // ERRORHANDLER_HPP