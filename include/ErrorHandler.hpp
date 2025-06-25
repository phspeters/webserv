#ifndef ERRORHANDLER_HPP
#define ERRORHANDLER_HPP

#include "webserv.hpp"

struct Connection;
struct HttpResponse;
struct VirtualServer;
enum ParseStatus;
enum ResponseStatus;

// Centralized error handling for HTTP responses
namespace ErrorHandler {

// ==================== ERROR RESPONSE GENERATORS ================
void generate_error_response(Connection* conn, ResponseStatus response_status);
void generate_error_response(Connection* conn, ParseStatus parse_status);

// ==================== ERROR INFO MAPPING =======================
int get_parse_message_status(ParseStatus parse_status);

// ==================== CORE ERROR HANDLING ======================
void handle_error(HttpResponse* resp, int status_code,
                  const VirtualServer& virtual_server);

// ==================== ERROR PAGE GENERATION ====================
std::string get_error_page_content(int status_code,
                                   const VirtualServer& virtual_server);
std::string generate_default_error_page(int status_code,
                                        const std::string& status_message);

}  // namespace ErrorHandler

#endif  // ERRORHANDLER_HPP