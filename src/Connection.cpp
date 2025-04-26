#include "webserv.hpp"

Connection::Connection(int client_fd, const ServerConfig* config)
    : client_fd_(client_fd),
      server_config_(config),
      last_activity_(time(NULL)),
      close_scheduled_(false),
      read_buffer_(),
      write_buffer_(),
      write_buffer_offset_(0),
      request_data_(NULL),
      response_data_(NULL),
      state_(CONN_READING),
      active_handler_ptr_(NULL) {}

Connection::~Connection() {
    // Placeholder for destructor implementation
    if (request_data_) {
        delete request_data_;
    }
    if (response_data_) {
        delete response_data_;
    }
}
