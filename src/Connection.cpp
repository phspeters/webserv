#include "webserv.hpp"

Connection::Connection(int client_fd, const ServerConfig& config)
    : client_fd_(client_fd),
      server_config_(config),
      last_activity_(time(NULL)),
      read_buffer_(),
      write_buffer_(),
      write_buffer_offset_(0),
      request_data_(NULL),
      response_data_(NULL),
      state_(CONN_READING),
      active_handler_ptr_(NULL) {}

Connection::~Connection() {
    if (request_data_) {
        delete request_data_;
    }

    if (response_data_) {
        delete response_data_;
    }

    if (client_fd_ >= 0) {
        close(client_fd_);
    }
}

// TODO implement cleanup for file_upload_handler
void Connection::reset_for_keep_alive() {
    // Reset buffers
    read_buffer_.clear();
    write_buffer_.clear();
    write_buffer_offset_ = 0;

    // Clean up request/response
    if (request_data_) {
        delete request_data_;
        request_data_ = NULL;
    }
    if (response_data_) {
        delete response_data_;
        response_data_ = NULL;
    }

    // Close any open file descriptors
    if (static_file_fd_ >= 0) {
        close(static_file_fd_);
        static_file_fd_ = -1;
    }
    if (cgi_pipe_stdin_fd_ >= 0) {
        close(cgi_pipe_stdin_fd_);
        cgi_pipe_stdin_fd_ = -1;
    }
    if (cgi_pipe_stdout_fd_ >= 0) {
        close(cgi_pipe_stdout_fd_);
        cgi_pipe_stdout_fd_ = -1;
    }

    // Reset state variables
    state_ = CONN_READING;
    active_handler_ptr_ = NULL;
    static_file_offset_ = 0;
    static_file_bytes_to_send_ = 0;
    cgi_pid_ = -1;

    // Reset activity timer
    last_activity_ = time(NULL);
}

bool Connection::is_readable() const {
    return state_ == CONN_READING || state_ == CONN_PROCESSING;
}

bool Connection::is_writable() const {
    return state_ == CONN_WRITING || state_ == CONN_CGI_EXEC;
}

bool Connection::is_error() const { return state_ == CONN_ERROR; }

bool Connection::is_keep_alive() const {
    if (!request_data_) return false;

    // For HTTP/1.0: requires explicit "Connection: keep-alive"
    if (request_data_->version_ == "HTTP/1.0") {
        std::string connection = request_data_->getHeader("Connection");
        return connection.find("keep-alive") != std::string::npos;
    }

    // For HTTP/1.1: keep-alive by default unless "Connection: close"
    std::string connection = request_data_->getHeader("Connection");
    return connection.find("close") == std::string::npos;
}
