#ifndef ERRORHANDLER_HPP
#define ERRORHANDLER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
struct HttpResponse;
struct VirtualServer;

// Centralized error handling for HTTP responses
namespace ErrorHandler {

// ==================== MAIN ERROR RESPONSE GENERATORS ====================
void generate_error_response(Connection* conn);
void generate_error_response(Connection* conn, codes::ResponseStatus status);

// ==================== ERROR INFO MAPPING ====================
void get_error_info_from_parse_status(codes::ParseStatus parse_status, 
                                     int& status_code, 
                                     std::string& message);

void get_error_info_from_response_status(codes::ResponseStatus response_status,
                                       int& status_code,
                                       std::string& message);

// ==================== CORE ERROR HANDLING ====================
void handle_error(HttpResponse* resp, int status_code,
                  const VirtualServer& virtual_server);

// ==================== SPECIFIC ERROR METHODS ====================
void bad_request(HttpResponse* resp, const VirtualServer& virtual_server);
void unauthorized(HttpResponse* resp, const VirtualServer& virtual_server);
void forbidden(HttpResponse* resp, const VirtualServer& virtual_server);
void not_found(HttpResponse* resp, const VirtualServer& virtual_server);
void method_not_allowed(HttpResponse* resp, const VirtualServer& virtual_server);
void request_timeout(HttpResponse* resp, const VirtualServer& virtual_server);
void payload_too_large(HttpResponse* resp, const VirtualServer& virtual_server);
void unsupported_media_type(HttpResponse* resp, const VirtualServer& virtual_server);
void insufficient_storage(HttpResponse* resp, const VirtualServer& virtual_server); 
void too_many_requests(HttpResponse* resp, const VirtualServer& virtual_server,
                       int retry_after = 60);
void internal_server_error(HttpResponse* resp, const VirtualServer& virtual_server);
void not_implemented(HttpResponse* resp, const VirtualServer& virtual_server);
void bad_gateway(HttpResponse* resp, const VirtualServer& virtual_server);
void service_unavailable(HttpResponse* resp, const VirtualServer& virtual_server);
void gateway_timeout(HttpResponse* resp, const VirtualServer& virtual_server);
void http_version_not_supported(HttpResponse* resp, const VirtualServer& virtual_server);

// ==================== CONNECTION UTILITIES ====================
void apply_to_connection(Connection* conn, int status_code,
                         const VirtualServer& virtual_server);

// ==================== ERROR PAGE GENERATION ====================
std::string get_error_page_content(int status_code,
                                   const VirtualServer& virtual_server);
std::string generate_default_error_page(int status_code,
                                        const std::string& status_message);

}  // namespace ErrorHandler

#endif  // ERRORHANDLER_HPP