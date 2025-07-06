#ifndef LOGGER_HPP
#define LOGGER_HPP

#include "common.hpp"

#define RESET "\x1B[0m"
#define LIGHT_RED "\x1B[31m"
#define RED "\x1B[91m"
#define WHITE "\x1B[37m"
#define LIGHT_GREY "\x1B[90m"
#define BLINK "\x1b[5m"
#define YELLOW "\x1B[33m"
#define LIGHT_BLUE "\x1B[94m"
#define CYAN "\x1B[36m"
#define MAGENTA "\x1B[95m"

int log(log_level level, const char* msg, ...);
void log_request(log_level level, const Connection* conn);
void log_response(log_level level, const Connection* conn);
int log_buffer(log_level level, const Buffer& buffer);
void log_virtual_server(log_level level, const VirtualServer& virtual_server);

#endif  // LOGGER_HPP