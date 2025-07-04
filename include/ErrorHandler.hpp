#ifndef ERRORHANDLER_HPP
#define ERRORHANDLER_HPP

#include "common.hpp"

struct Connection;
struct HttpResponse;
struct VirtualServer;

// Centralized error handling for HTTP responses
namespace ErrorHandler {

// ==================== ERROR RESPONSE GENERATORS ================
void generate_error_response(Connection* conn, ResponseStatus response_status);
void generate_error_response(Connection* conn, ParseStatus parse_status);

// ==================== ERROR PAGE GENERATION ====================
int get_error_page(int status_code, const VirtualServer& virtual_server);
std::string generate_default_error_page(ResponseStatus status_code,
                                        const std::string& status_message);

// ==================== ERROR INFO MAPPING =======================
ResponseStatus parse_status_to_response_status(ParseStatus parse_status);

}  // namespace ErrorHandler

#endif  // ERRORHANDLER_HPP