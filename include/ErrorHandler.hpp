#ifndef ERRORHANDLER_HPP
#define ERRORHANDLER_HPP

#include "common.hpp"

struct Connection;
struct VirtualServer;

namespace ErrorHandler {

void generate_error_response(Connection* conn, HttpStatus response_status);
void generate_error_response(Connection* conn, ParseStatus parse_status);

int get_error_page(int status_code, const VirtualServer& virtual_server);
std::string generate_default_error_page(HttpStatus status_code,
                                        const std::string& status_message);

HttpStatus parse_status_to_response_status(ParseStatus parse_status);

}  // namespace ErrorHandler

#endif  // ERRORHANDLER_HPP