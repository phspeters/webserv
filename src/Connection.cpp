#include "common.hpp"

Connection::Connection(WebServer* owner, int fd,
                       const VirtualServer* default_virtual_server)
    : owner_server_(owner),
      client_fd_(fd),
      default_virtual_server_(default_virtual_server),
      virtual_server_(default_virtual_server),
      last_activity_(time(NULL)),
      request_data_(new HttpRequest()),
      response_data_(new HttpResponse()),
      status_(OK),
      conn_state_(CONN_READING_REQUEST),
      active_handler_(NULL),
      location_match_(NULL),
      is_asynchronous_(true),
      static_file_context_(NULL),
      file_upload_context_(NULL),
      cgi_context_(NULL) {
    IOContext* client_ctx =
        add_io_context(client_fd_, FD_CLIENT_SOCKET, EPOLLIN);
    if (!client_ctx) {
        throw std::runtime_error(
            "Failed to create and register IOContext for new connection");
    }
    log(LOG_DEBUG, "Connection created for socket '%i'", client_fd_);
}

Connection::~Connection() {
    // Clean up owned resources
    if (request_data_) {
        delete request_data_;
    }

    if (response_data_) {
        delete response_data_;
    }

    // Clean up I/O contexts
    while (!io_contexts_.empty()) {
        remove_io_context(io_contexts_.begin()->second);
    }

    // Close any open file descriptors
    if (client_fd_ >= 0) {
        close(client_fd_);
        client_fd_ = -1;
    }

    // Clean up handler contexts (if any)
    if (static_file_context_) {
        delete static_file_context_;
    }

    if (file_upload_context_) {
        delete file_upload_context_;
    }

    if (cgi_context_) {
        delete cgi_context_;
    }

    log(LOG_TRACE, "Connection resources cleaned up for socket '%i'",
        client_fd_);
}

IOContext* Connection::add_io_context(int fd, FdType type, uint32_t events) {
    std::map<FdType, IOContext*>::iterator it = io_contexts_.find(type);
    if (it != io_contexts_.end()) {
        log(LOG_FATAL,
            "I/O context for type '%d' already exists for socket '%i'", type,
            fd);
        return NULL;
    }

    IOContext* io_context = NULL;
    try {
        io_context = new IOContext(fd, type, this);

        // Add to epoll
        if (!owner_server_->add_context_to_epoll(io_context, events)) {
            log(LOG_ERROR, "Failed to add I/O context for socket '%i'",
                io_context->fd_);
            delete io_context;  // Clean up if failed
            return NULL;
        }

        // Add to the list of contexts
        io_contexts_[type] = io_context;
        log(LOG_DEBUG, "I/O context added for socket '%i'", io_context->fd_);

        return io_context;

    } catch (const std::bad_alloc& e) {
        log(LOG_ERROR, "Memory allocation failed for I/O context: %s",
            e.what());
        if (io_context) {
            delete io_context;  // Clean up if allocation failed
        }
        return NULL;
    }
}

void Connection::remove_io_context(IOContext* io_context) {
    std::map<FdType, IOContext*>::iterator it =
        io_contexts_.find(io_context->type_);
    if (it == io_contexts_.end()) {
        log(LOG_WARNING, "No I/O context found for type '%d' on socket '%i'",
            io_context->type_, io_context->fd_);
        return;
    }

    if (it->second != io_context) {
        log(LOG_WARNING, "I/O context mismatch for type '%d' on socket '%i'",
            io_context->type_, io_context->fd_);
        return;
    }

    // Remove from epoll
    if (!owner_server_->remove_context_from_epoll(io_context)) {
        log(LOG_ERROR, "Failed to remove I/O context for socket '%i'",
            io_context->fd_);  // Log error but continue cleanup
    }

    // Clean up
    log(LOG_DEBUG, "I/O context removed for socket '%i'", io_context->fd_);
    delete io_context;
    io_contexts_.erase(it);
}

bool Connection::is_keep_alive() const {
    if (!request_data_ || !response_data_) {
        log(LOG_FATAL, "Invalid request/response data for socket '%i'",
            client_fd_);
        return false;
    }

    log(LOG_TRACE, "Checking keep-alive for socket '%i'", client_fd_);

    int status = response_data_->status_code_;
    if (is_fatal_error_status(status)) {
        log(LOG_DEBUG,
            "Fatal error status %d detected for socket '%i', not keeping alive",
            status, client_fd_);
        return false;
    }

    // For HTTP/1.0: requires explicit "Connection: keep-alive"
    if (request_data_->version_ == "HTTP/1.0") {
        std::string connection = request_data_->get_header("Connection");
        return connection.find("keep-alive") != std::string::npos;
    }

    // For HTTP/1.1: keep-alive by default unless "Connection: close"
    std::string connection = request_data_->get_header("Connection");
    return connection.find("close") == std::string::npos;
}

bool Connection::is_fatal_error_status(int status) const {
    return status == 400 || status == 408 || status == 413 || status == 414 ||
           status == 431 || status >= 500;
}

void Connection::reset_for_keep_alive() {
    // Reset virtual server
    virtual_server_ = default_virtual_server_;

    // Reset buffers
    read_buffer_.prepare_for_next_request();
    write_buffer_.reset();

    // Reset request/response
    if (request_data_) {
        request_data_->clear();
    }
    if (response_data_) {
        response_data_->clear();
    }

    // Reset state variables
    parser_context_.reset();
    writer_context_.reset();
    location_match_ = NULL;
    active_handler_ = NULL;
    is_asynchronous_ = true;

    // Clean up handler contexts (if any)
    if (static_file_context_) {
        delete static_file_context_;
        static_file_context_ = NULL;
    }

    if (file_upload_context_) {
        delete file_upload_context_;
        file_upload_context_ = NULL;
    }

    if (cgi_context_) {
        delete cgi_context_;
        cgi_context_ = NULL;
    }

    // Reset activity timer
    last_activity_ = time(NULL);

    owner_server_->update_context_in_epoll(io_contexts_[FD_CLIENT_SOCKET], EPOLLIN);

    status_ = OK;
    conn_state_ = CONN_READING_REQUEST;

    log(LOG_DEBUG, "Connection reset for keep-alive on socket '%i'",
        client_fd_);
}
