#ifndef LOGGER_HPP
#define LOGGER_HPP

#include "webserv.hpp"

void print_request(const Connection* conn);
void print_response(const Connection* conn);
int print_and_erase_buffer(std::vector<char>& buffer);
void print_server_config(const ServerConfig& config);
void log_client_error(int status_code, const Connection* conn, const ServerConfig& config);


#endif  // LOGGER_HPP