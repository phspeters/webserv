#ifndef LOGGER_HPP
#define LOGGER_HPP

#include "webserv.hpp"

void print_request(const Connection* conn);
void print_response(const Connection* conn);
int print_buffer(std::vector<char>& buffer);
int log(const std::string& message);

void build_mock_response(Connection* conn);

#endif  // LOGGER_HPP