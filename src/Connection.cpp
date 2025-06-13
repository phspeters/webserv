#include "webserv.hpp"

Connection::Connection(int fd, const VirtualServer* default_virtual_server)
    : client_fd_(fd),
      default_virtual_server_(default_virtual_server),
      virtual_server_(default_virtual_server),
      last_activity_(time(NULL)),
      chunk_remaining_bytes_(0),
      write_buffer_offset_(0),
      cgi_read_buffer_offset_(0),
      request_data_(new HttpRequest()),
      response_data_(new HttpResponse()),
      conn_state_(codes::CONN_READING),
      parser_state_(codes::PARSING_REQUEST_LINE),
      cgi_handler_state_(codes::CGI_HANDLER_IDLE),
      parse_status_(codes::PARSE_INCOMPLETE),
      write_status_(codes::WRITING_INCOMPLETE),
      active_handler_(NULL),        
      location_match_(NULL),        
      cgi_pid_(-1),                
      cgi_pipe_stdin_fd_(-1),
      cgi_pipe_stdout_fd_(-1),
      cgi_script_path_(""),
      cgi_envp_(),
      static_file_fd_(-1),
      static_file_offset_(0),
      static_file_bytes_to_send_(0) {}

Connection::~Connection() {
    // Clean up owned resources
    if (request_data_) {
        delete request_data_;
    }

    if (response_data_) {
        delete response_data_;
    }

    // Close any open file descriptors
    if (client_fd_ >= 0) {
        close(client_fd_);
    }
    if (static_file_fd_ >= 0) {
        close(static_file_fd_);
    }
    WebServer* web_server = WebServer::get_instance();
    if (cgi_pipe_stdin_fd_ >= 0) {
        web_server->get_conn_manager()->unregister_pipe(cgi_pipe_stdin_fd_);
        close(cgi_pipe_stdin_fd_);
    }
    if (cgi_pipe_stdout_fd_ >= 0) {
        web_server->get_conn_manager()->unregister_pipe(cgi_pipe_stdout_fd_);
        close(cgi_pipe_stdout_fd_);
    }

    log(LOG_TRACE, "Connection resources cleaned up for socket '%i'",
        client_fd_);
}

void Connection::reset_for_keep_alive() {
    // Reset virtual server
    virtual_server_ = default_virtual_server_;

    // Reset write buffer and offsets
    chunk_remaining_bytes_ = 0;
    write_buffer_.clear();
    write_buffer_offset_ = 0;
    cgi_read_buffer_.clear();
    cgi_read_buffer_offset_ = 0;

    // Reset request/response
    if (request_data_) {
        request_data_->clear();
    }
    if (response_data_) {
        response_data_->clear();
    }

    // Reset state variables
    conn_state_ = codes::CONN_READING;
    parser_state_ = codes::PARSING_REQUEST_LINE;
    cgi_handler_state_ = codes::CGI_HANDLER_IDLE;
    parse_status_ = codes::PARSE_INCOMPLETE;
    write_status_ = codes::WRITING_INCOMPLETE;

    // Reset location match
    location_match_ = NULL;

    // Reset active handler
    active_handler_ = NULL;

    // Close any open file descriptors
    if (static_file_fd_ >= 0) {
        close(static_file_fd_);
        static_file_fd_ = -1;
    }
    WebServer* web_server = WebServer::get_instance();
    if (cgi_pipe_stdin_fd_ >= 0) {
        web_server->get_conn_manager()->unregister_pipe(cgi_pipe_stdin_fd_);
        close(cgi_pipe_stdin_fd_);
        cgi_pipe_stdin_fd_ = -1;
    }
    if (cgi_pipe_stdout_fd_ >= 0) {
        web_server->get_conn_manager()->unregister_pipe(cgi_pipe_stdout_fd_);
        close(cgi_pipe_stdout_fd_);
        cgi_pipe_stdout_fd_ = -1;
    }

    // Reset Handler-Specific State
    static_file_offset_ = 0;
    static_file_bytes_to_send_ = 0;
    cgi_pid_ = -1;
    cgi_script_path_.clear();
    cgi_envp_.clear();

    // Reset activity timer
    last_activity_ = time(NULL);

    log(LOG_DEBUG, "Connection reset for keep-alive on socket '%i'",
        client_fd_);
}

bool Connection::is_readable() const {
    return conn_state_ == codes::CONN_READING;
}

bool Connection::is_cgi() const { return conn_state_ == codes::CONN_CGI_EXEC; }

bool Connection::is_writable() const {
    return conn_state_ == codes::CONN_PROCESSING ||
           conn_state_ == codes::CONN_WRITING;
}

bool Connection::is_keep_alive() const {
    if (!request_data_) {
        log(LOG_FATAL, "Request data is invalid for socket '%i'", client_fd_);
        return false;
    }

    log(LOG_TRACE, "Checking keep-alive for socket '%i'", client_fd_);

    // For HTTP/1.0: requires explicit "Connection: keep-alive"
    if (request_data_->version_ == "HTTP/1.0") {
        std::string connection = request_data_->get_header("Connection");
        return connection.find("keep-alive") != std::string::npos;
    }

    // For HTTP/1.1: keep-alive by default unless "Connection: close"
    std::string connection = request_data_->get_header("Connection");
    return connection.find("close") == std::string::npos;
}
