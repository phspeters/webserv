#ifndef LOGGER_HPP
#define LOGGER_HPP

#include "webserv.hpp"

#define RESET "\x1B[0m"
#define RED "\x1B[31m"
#define LIGHT_RED "\x1B[91m"
#define WHITE "\x1B[37m"
#define BLINK "\x1b[5m"
#define YELLOW "\x1B[33m"
#define LIGHT_BLUE "\x1B[94m"
#define CYAN "\x1B[36m"
#define MAGENTA "\x1B[95m"

#define ACTIVE_LOG_LEVEL LOG_DEBUG

enum log_level {
    LOG_OFF,
    LOG_TRACE,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_FATAL
};

int log(log_level level, const char* msg, ...);
void log_request(log_level level, const Connection* conn);
void log_response(log_level level, const Connection* conn);
int log_buffer(log_level level, std::vector<char>& buffer);

void log_virtual_server(log_level level,
                        const VirtualServer& virtual_server);
void log_client_error(int status_code, const Connection* conn,
                      const VirtualServer& config);
std::string get_current_gmt_time();

#endif  // LOGGER_HPP