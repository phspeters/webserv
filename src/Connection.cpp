#include "webserv.hpp"

Connection::Connection(int client_fd, const ServerConfig* _config)
    : client_fd(client_fd),
      _server_config(_config),
      last_activity(time(NULL)),
      close_scheduled(false),
      read_buffer(),
      write_buffer(),
      write_buffer_offset(0),
      request_data(NULL),
      response_data(NULL),
      state(CONN_READING),
      active_handler_ptr(NULL) {}

Connection::~Connection() {
    // Placeholder for destructor implementation
    if (request_data) {
        delete request_data;
    }
    if (response_data) {
        delete response_data;
    }
}
