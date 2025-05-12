#ifndef LOGGER_HPP
#define LOGGER_HPP

#include "webserv.hpp"

void print_request(const Connection* conn);
void print_response(const Connection* conn);
int print_and_erase_buffer(std::vector<char>& buffer);

#endif  // LOGGER_HPP