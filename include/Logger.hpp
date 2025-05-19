#ifndef LOGGER_HPP
#define LOGGER_HPP

#include "webserv.hpp"

void print_request(const Connection* conn);
void print_response(const Connection* conn);
int print_buffer(std::vector<char>& buffer);

void print_virtual_server(const VirtualServer& config);
void log_client_error(int status_code, const Connection* conn,
                      const VirtualServer& config);

// TEMP
void build_mock_response(Connection* conn);

#endif  // LOGGER_HPP