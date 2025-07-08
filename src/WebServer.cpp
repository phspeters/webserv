#include "common.hpp"

WebServer* WebServer::instance_ = NULL;

WebServer::WebServer() : epoll_fd_(-1), ready_(false) { instance_ = this; }

WebServer::~WebServer() {
    // Close listener sockets if they are open
    while (!listener_contexts_.empty()) {
        remove_listener_context(listener_contexts_.back());
    }

    // Close all active connections
    while (!active_connections_.empty()) {
        close_client_connection(active_connections_.begin()->second);
    }

    // Close the epoll instance if it was created
    if (epoll_fd_ >= 0) {
        log(LOG_TRACE, "Closing epoll instance: %d", epoll_fd_);
        close(epoll_fd_);
    }

    log(LOG_INFO, "WebServer resources cleaned up");
}

bool WebServer::init() {
    if (!setup_signal_handlers()) {
        log(LOG_ERROR, "Failed to set up signal handlers");
        return false;
    }

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        log(LOG_ERROR, "Failed to create epoll instance");
        return false;
    }

    if (!setup_listener_sockets()) {
        return false;
    }

    log(LOG_INFO, "WebServer initialized successfully");

    return true;
}

bool WebServer::parse_config_file(const std::string& filename) {
    std::string::size_type pos = filename.find_last_of(".");
    if (pos == std::string::npos || filename.substr(pos) != ".conf") {
        log(LOG_ERROR, "Error: Invalid configuration file extension: %s",
            filename.c_str());
        return false;
    }

    std::ifstream file(filename.c_str());
    if (!file.is_open()) {
        log(LOG_ERROR, "Error: Could not open configuration file: %s",
            filename.c_str());
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);

        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Look for server block
        if (line == "server {" ||
            (line.find("server") == 0 && line.find("{") != std::string::npos)) {
            VirtualServer virtual_server;
            if (virtual_server.parse_server_block(file)) {
                if (!virtual_server.is_valid()) {
                    log(LOG_ERROR,
                        "Error: Invalid virtual server configuration");
                    return false;
                }

                log(LOG_DEBUG,
                    "Parsed valid virtual server configuration for host: %s, "
                    "port: %d",
                    virtual_server.host_.c_str(), virtual_server.port_);

                virtual_servers_.push_back(virtual_server);
            } else {
                log(LOG_ERROR, "Error parsing server block");
                return false;
            }

            log_virtual_server(LOG_TRACE, virtual_server);

        } else {
            log(LOG_ERROR, "Error parsing server block");
            return false;
        }
    }

    file.close();

    log(LOG_INFO, "Parsed %zu virtual servers from configuration file",
        virtual_servers_.size());
    return true;
}

void WebServer::run() {
    ready_ = true;

    log(LOG_INFO, "WebServer is ready and waiting for connections");
    event_loop();
}

void WebServer::shutdown() {
    ready_ = false;
    std::cout << std::endl;
    log(LOG_INFO, "WebServer shutdown initiated");
}

bool WebServer::add_context_to_epoll(IOContext* ctx, uint32_t events) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.ptr = ctx;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, ctx->fd_, &event) < 0) {
        log(LOG_ERROR, "Failed to register socket '%d' on epoll: %s", ctx->fd_,
            strerror(errno));
        return false;
    }

    log(LOG_TRACE, "Registered socket '%d' on epoll with events %u", ctx->fd_,
        events);
    return true;
}

bool WebServer::update_context_in_epoll(IOContext* ctx, uint32_t events) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.ptr = ctx;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, ctx->fd_, &event) < 0) {
        log(LOG_ERROR, "Failed to up epoll events for socket '%d'", ctx->fd_);
        return false;
    }

    log(LOG_TRACE, "Updated epoll events for socket '%d' to %u", ctx->fd_,
        events);
    return true;
}

bool WebServer::remove_context_from_epoll(IOContext* ctx) {
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, ctx->fd_, NULL) < 0) {
        log(LOG_ERROR, "Failed to unregister socket '%d' on epoll", ctx->fd_);
        return false;
    }

    log(LOG_TRACE, "Unregistered socket '%d' on epoll", ctx->fd_);
    return true;
}

void WebServer::event_loop() {
    struct epoll_event events[MAX_EPOLL_EVENTS];

    while (ready_) {
        int timed_out = cleanup_timed_out_connections();
        if (timed_out > 0) {
            log(LOG_INFO, "Closed '%d' timed out connections.", timed_out);
        }

        int ready_events = epoll_wait(epoll_fd_, events, MAX_EPOLL_EVENTS,
                                      http_limits::TIMEOUT);
        if (ready_events < 0) {
            if (errno == EINTR) {
                // Interrupted by a signal, continue the loop
                log(LOG_DEBUG, "event_loop: epoll_wait interrupted by signal");
                continue;
            }
            log(LOG_ERROR, "event_loop: epoll_wait error: %s", strerror(errno));
            break;
        }

        if (ready_events > 0) {
            log(LOG_INFO, "event_loop: Processing %d ready events",
                ready_events);
        }

        for (int i = 0; i < ready_events; i++) {
            // The IOContext pointer gives us ALL the context we need.
            IOContext* ctx = static_cast<IOContext*>(events[i].data.ptr);
            uint32_t event_flags = events[i].events;

            if (event_flags & (EPOLLERR | EPOLLHUP)) {
                log(LOG_ERROR, "Epoll error or hangup on fd %d (type: %d)",
                    ctx->fd_, ctx->type_);

                if (ctx->type_ == FD_LISTENER) {
                    remove_listener_context(ctx);
                } else {
                    close_client_connection(ctx->conn_);
                }

                continue;
            }

            // Based on fd type, we call a specific handle function
            switch (ctx->type_) {
                case FD_LISTENER:
                    accept_new_connection(ctx->fd_);
                    break;
                case FD_CLIENT_SOCKET:
                    handle_client_socket_event(ctx, event_flags);
                    break;
                case FD_CGI_PIPE_WRITE:
                    handle_cgi_write_event(ctx, event_flags);
                    break;
                case FD_CGI_PIPE_READ:
                    handle_cgi_read_event(ctx, event_flags);
                    break;
            }
        }
    }
    log(LOG_INFO, "event_loop: Server event loop terminated");
}

void WebServer::close_client_connection(Connection* conn) {
    int fd = conn->client_fd_;
    std::map<int, Connection*>::iterator it = active_connections_.find(fd);

    if (it != active_connections_.end()) {
        delete it->second;
        log(LOG_INFO, "Closed connection for client (fd: %d)", fd);
        active_connections_.erase(it);
    } else {
        log(LOG_FATAL, "Connection not found for socket '%d'", fd);
    }
}

bool WebServer::read_from_client_socket(Connection* conn) {
    log(LOG_TRACE, "WebServer::read_from_client_socket called for client %d",
        conn->client_fd_);

    ssize_t bytes_read = conn->read_buffer_.read_from(conn->client_fd_);

    if (bytes_read == 0) {
        log(LOG_WARNING, "Client disconnected (fd: %d)", conn->client_fd_);
        return false;
    }

    if (bytes_read == -1) {
        log(LOG_ERROR, "Error reading from socket (fd: %d): %s",
            conn->client_fd_, strerror(errno));
        return false;
    }

    if (bytes_read == BUFFER_FULL) {
        log(LOG_DEBUG, "Buffer full while reading from socket (fd: %d)",
            conn->client_fd_);
        return true;
    }

    conn->last_activity_ = time(NULL);

    log(LOG_INFO, "Read %zd bytes from socket (fd: %d)", bytes_read,
        conn->client_fd_);

    log_buffer(LOG_TRACE, conn->read_buffer_);

    return true;
}

bool WebServer::setup_listener_sockets() {
    std::map<std::pair<std::string, int>, int> listener_fds;

    for (std::list<VirtualServer>::iterator it = virtual_servers_.begin();
         it != virtual_servers_.end(); ++it) {
        VirtualServer* vs = &(*it);
        std::pair<std::string, int> listen_addr(vs->host_, vs->port_);

        // If we haven't created a listener for this address yet, do it now.
        if (listener_fds.find(listen_addr) == listener_fds.end()) {
            int fd = create_listener_socket(vs->host_, vs->port_);
            if (fd < 0) {
                return false;
            }

            if (!add_listener_context(fd)) {
                return false;
            }

            listener_fds[listen_addr] = fd;
            log(LOG_INFO, "Listening on %s:%d (fd: %d)", vs->host_.c_str(),
                vs->port_, fd);
        }
        // Associate this virtual server with the listener fd
        listener_to_virtual_servers_[listener_fds[listen_addr]].push_back(vs);
    }
    return true;
}

int WebServer::create_listener_socket(const std::string& host, int port) {
    log(LOG_DEBUG, "Creating listener socket for host: %s on port: %d",
        host.c_str(), port);

    struct addrinfo filter, *results, *current;
    int listener_fd = -1;
    char port_str[6];  // max port is 65535 (5 digits) + null terminator
    snprintf(port_str, sizeof(port_str), "%d", port);

    memset(&filter, 0, sizeof(filter));
    filter.ai_family = AF_INET;
    filter.ai_socktype = SOCK_STREAM;
    filter.ai_flags = AI_PASSIVE;  // Important for bind():
    // if the host parameter is NULL or a wildcard string like "0.0.0.0", this
    // flag makes getaddrinfo fill in the IP address field with the correct
    // wildcard address (INADDR_ANY), which allows your server to accept
    // connections on any available network interface.

    // getaddrinfo does the DNS/hosts lookup
    int status = getaddrinfo(host.c_str(), port_str, &filter, &results);
    if (status != 0) {
        log(LOG_ERROR, "getaddrinfo error for %s:%d: %s", host.c_str(), port,
            gai_strerror(status));
        return -1;
    }

    // Loop through results and bind to the first one we can
    for (current = results; current != NULL; current = current->ai_next) {
        listener_fd =
            socket(current->ai_family, current->ai_socktype | SOCK_NONBLOCK,
                   current->ai_protocol);
        if (listener_fd < 0) {
            continue;  // Try next address
        }

        int opt = 1;
        if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &opt,
                       sizeof(opt)) < 0) {
            close(listener_fd);
            continue;  // Try next address
        }

        if (bind(listener_fd, current->ai_addr, current->ai_addrlen) < 0) {
            close(listener_fd);
            continue;  // Try next address
        }

        break;  // Successfully bound
    }

    if (current == NULL) {
        log(LOG_ERROR, "Failed to bind to %s:%d", host.c_str(), port);
        freeaddrinfo(results);
        return -1;
    }

    freeaddrinfo(results);

    if (listen(listener_fd, SOMAXCONN) < 0) {
        close(listener_fd);
        log(LOG_ERROR, "Failed to listen on %s:%d", host.c_str(), port);
        return -1;
    }

    log(LOG_INFO, "Successfully created listener socket for %s:%d on fd %d",
        host.c_str(), port, listener_fd);
    return listener_fd;
}

int WebServer::cleanup_timed_out_connections() {
    int closed = 0;
    time_t current_time = time(NULL);

    std::map<int, Connection*>::iterator it = active_connections_.begin();
    while (it != active_connections_.end()) {
        Connection* conn = it->second;
        if ((current_time - conn->last_activity_) > http_limits::TIMEOUT) {
            log(LOG_WARNING,
                "Connection (fd: %d) timed out after %ld seconds, closing",
                conn->client_fd_, http_limits::TIMEOUT);

            std::map<int, Connection*>::iterator to_erase = it;
            ++it;

            close_client_connection(to_erase->second);
            closed++;
        } else {
            ++it;
        }
    }

    return closed;
}

bool WebServer::add_listener_context(int listener_fd) {
    if (listener_fd < 0) {
        log(LOG_FATAL, "add_listener_context: Invalid listener_fd '%d'",
            listener_fd);
        return false;
    }

    try {
        IOContext* ctx = new IOContext(listener_fd, FD_LISTENER, NULL);
        if (!add_context_to_epoll(ctx, EPOLLIN)) {
            log(LOG_ERROR, "Failed to add listener socket '%d' to epoll",
                ctx->fd_);
            close(ctx->fd_);
            delete ctx;
            return false;
        }

        listener_contexts_.push_back(ctx);
        log(LOG_INFO, "Listener socket '%d' added successfully", ctx->fd_);
        return true;
    } catch (const std::exception& e) {
        log(LOG_ERROR,
            "Failed to create IOContext for listener socket '%d': %s",
            listener_fd, e.what());
        close(listener_fd);
        return false;
    }
}

bool WebServer::remove_listener_context(IOContext* ctx) {
    if (!ctx) {
        log(LOG_FATAL, "remove_listener_context: NULL context provided");
        return false;
    }

    remove_context_from_epoll(ctx);
    close(ctx->fd_);

    std::vector<IOContext*>::iterator it =
        std::find(listener_contexts_.begin(), listener_contexts_.end(), ctx);

    bool found = (it != listener_contexts_.end());
    if (found) {
        listener_contexts_.erase(it);
    } else {
        log(LOG_ERROR,
            "Listener socket '%d' not found in managed contexts during removal",
            ctx->fd_);
    }

    log(LOG_INFO, "Listener socket '%d' removed and cleaned up successfully",
        ctx->fd_);
    delete ctx;

    return found;
}

bool WebServer::setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGINT, &sa, NULL) < 0) {
        log(LOG_ERROR, "Failed to set up SIGINT handler");
        return false;
    }

    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        log(LOG_ERROR, "Failed to set up SIGTERM handler");
        return false;
    }

    // TODO: Remove this handler and only use sockets and send?
    if (sigaction(SIGPIPE, &sa, NULL) < 0) {
        log(LOG_ERROR, "Failed to set up SIGPIPE handler");
        return false;
    }

    return true;
}

void WebServer::signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        get_instance()->shutdown();
        log(LOG_INFO, "Received shutdown signal. Exiting...");
    } else if (signal == SIGPIPE) {
        // Ignore SIGPIPE to prevent crashes on broken pipes
        log(LOG_DEBUG, "Received SIGPIPE, ignoring");
    }
}

void WebServer::accept_new_connection(int listener_fd) {
    log(LOG_DEBUG,
        "WebServer::accept_new_connection called for listener socket"
        "%d",
        listener_fd);

    VirtualServer* default_server = NULL;
    if (listener_to_virtual_servers_.find(listener_fd) !=
        listener_to_virtual_servers_.end()) {
        default_server = listener_to_virtual_servers_[listener_fd].front();
    } else {
        log(LOG_FATAL, "No default server found for listener socket '%d'",
            listener_fd);
        return;
    }

    int client_fd = accept4(listener_fd, NULL, NULL, SOCK_NONBLOCK);
    if (client_fd < 0) {
        log(LOG_ERROR, "Failed to accept new connection listener socket '%d'",
            listener_fd);
        return;
    }

    if (!create_client_connection(client_fd, default_server)) {
        return;
    }

    log(LOG_INFO,
        "Accepted new connection on listener socket '%d' for client %d",
        listener_fd, client_fd);
}

Connection* WebServer::create_client_connection(
    int client_fd, const VirtualServer* default_virtual_server) {
    log(LOG_TRACE, "WebServer::create_client_connection called for client %d",
        client_fd);

    try {
        Connection* conn =
            new Connection(this, client_fd, default_virtual_server);

        active_connections_[client_fd] = conn;
        log(LOG_INFO, "Accepted new connection from client (fd: %d)",
            client_fd);
        return conn;

    } catch (const std::exception& e) {
        log(LOG_ERROR, "Failed to create connection for fd %d: %s", client_fd,
            e.what());
        close(client_fd);
        return NULL;
    }
}

void WebServer::handle_client_socket_event(IOContext* ctx,
                                           uint32_t event_flags) {
    if (!ctx) {
        log(LOG_FATAL,
            "handle_client_socket_event: Connection pointer is NULL");
        return;
    }

    Connection* conn = ctx->conn_;
    log(LOG_INFO, "New %s event for client %d", event_to_string(event_flags),
        conn->client_fd_);

    if (event_flags & EPOLLIN) {
        if (!read_from_client_socket(conn)) {
            close_client_connection(conn);
            return;
        }

        if (conn->conn_state_ == CONN_READING_REQUEST ||
            conn->conn_state_ == CONN_GENERATING_RESPONSE) {
            ParseStatus status = handle_request_parsing(conn);
            conn->status_ =
                ErrorHandler::parse_status_to_response_status(status);
            if (status >= PARSE_ERROR) {
                handle_error_response(conn);
                return;
            }
            log_request(LOG_TRACE, conn);
        }
    }

    if (event_flags & EPOLLOUT) {
        if (conn->conn_state_ == CONN_WRITING_RESPONSE) {
            log_buffer(LOG_TRACE, conn->write_buffer_);

            ssize_t bytes_sent = conn->write_buffer_.write_to(conn->client_fd_);
            if (bytes_sent <= 0) {
                log(LOG_ERROR,
                    "handle_client_socket_event: Error writing to socket for "
                    "client_fd %d: %s",
                    conn->client_fd_, strerror(errno));
                close_client_connection(conn);
                return;
            }

            log(LOG_DEBUG,
                "handle_client_socket_event: Wrote %zd bytes to socket for "
                "client_fd %d",
                bytes_sent, conn->client_fd_);

            response_writer_.write_response_to_buffer(conn);

            if (!conn->write_buffer_.empty()) {
                log(LOG_DEBUG,
                    "handle_client_socket_event: Incomplete write for "
                    "client_fd %d, remaining bytes: %zu",
                    conn->client_fd_, conn->write_buffer_.readable_bytes());
                return;  // Still data to write, wait for next EPOLLOUT event
            }

            if (conn->active_handler_) {
                conn->active_handler_->cleanup_handler(conn);
            }
            handle_keep_alive(conn);

            return;
        } else {
            log(LOG_FATAL,
                "handle_event: Unexpected state for client_fd %d: %d",
                conn->client_fd_, conn->conn_state_);
            close_client_connection(conn);
        }
    }
}

bool WebServer::handle_keep_alive(Connection* conn) {
    if (conn->is_keep_alive()) {
        log(LOG_DEBUG, "handle_keep_alive: Keep-alive enabled for client_fd %d",
            conn->client_fd_);
        conn->reset_for_keep_alive();
        return true;
    } else {
        log(LOG_DEBUG, "handle_keep_alive: Closing connection for client_fd %d",
            conn->client_fd_);
        close_client_connection(conn);
        return false;
    }
}

ParseStatus WebServer::handle_request_parsing(Connection* conn) {
    log(LOG_TRACE, "WebServer::handle_request_parsing called for client %d",
        conn->client_fd_);

    ParseStatus status = PARSE_SUCCESS;

    while (true) {
        ParserState& state = conn->parser_context_.parser_state_;

        switch (state) {
            case PARSER_READING_REQUEST_LINE:
                status = request_parser_.parse_request_line(conn);
                if (status == PARSE_SUCCESS) {
                    state = PARSER_READING_HEADERS;
                }
                break;

            case PARSER_READING_HEADERS:
                status = request_parser_.parse_headers(conn);
                if (status == PARSE_SUCCESS) {
                    state = PARSER_PROCESSING_REQUEST;
                }
                break;

            case PARSER_PROCESSING_REQUEST:
                status = process_request(conn);  // Validate headers, etc.
                if (status == PARSE_SUCCESS) {
                    // Determine if we need to read a body and how
                    state = determine_body_handling_state(conn);
                }
                break;

            case PARSER_READING_CONTENT_BODY:
                status = request_parser_.parse_content_body(conn);
                if (status == PARSE_SUCCESS) {
                    state = PARSER_COMPLETE;
                }
                break;

            case PARSER_READING_CHUNKED_BODY:
                status = request_parser_.parse_chunked_body(conn);
                if (status == PARSE_SUCCESS) {
                    state = PARSER_COMPLETE;
                }
                break;

            case PARSER_COMPLETE:
                log(LOG_INFO, "Request parsing complete for fd %d.",
                    conn->client_fd_);
                return PARSE_SUCCESS;

            default:
                log(LOG_ERROR, "Unknown parser state for fd %d.",
                    conn->client_fd_);
                status = PARSE_ERROR;
                break;
        }

        // Check status after each step.
        if (status == PARSE_INCOMPLETE) {
            log(LOG_DEBUG, "Parser needs more data for fd %d. Waiting.",
                conn->client_fd_);
            return status;  // Exit and wait for the next EPOLLIN event.
        }

        if (status >= PARSE_ERROR) {
            log(LOG_ERROR, "Parse error %d for fd %d.", status,
                conn->client_fd_);
            return status;
        }
    }
}

ParseStatus WebServer::process_request(Connection* conn) {
    log(LOG_TRACE, "WebServer::process_request called for client %d",
        conn->client_fd_);

    if (conn->conn_state_ == CONN_READING_REQUEST) {
        match_host_header(conn);
        conn->location_match_ =
            match_location(conn->virtual_server_, conn->request_data_->path_);

        ParseStatus status = validate_version(conn);
        if (status != PARSE_SUCCESS) {
            log(LOG_ERROR, "Invalid HTTP version in request for connection: %d",
                conn->client_fd_);
            return status;
        }

        status = validate_method(conn);
        if (status != PARSE_SUCCESS) {
            log(LOG_ERROR,
                "Invalid or unsupported method in request for connection: %d",
                conn->client_fd_);
            return status;
        }

        status = validate_body_handling(conn);
        if (status != PARSE_SUCCESS) {
            log(LOG_ERROR,
                "Invalid body handling in request for connection: %d",
                conn->client_fd_);
            return status;
        }
    }

    conn->active_handler_ = choose_handler(conn);

    Result result = conn->active_handler_->handle(conn);
    if (result == ERROR) {
        handle_error_response(conn);
        return PARSE_ERROR;
    }

    if (conn->active_handler_->is_asynchronous()) {
        log(LOG_INFO,
            "Asynchronous handler set for connection: %d, awaiting for epoll "
            "event.",
            conn->client_fd_);
        conn->conn_state_ = CONN_GENERATING_RESPONSE;
    } else {
        log(LOG_INFO,
            "Synchronous handler finished for fd %d. Writing response.",
            conn->client_fd_);
        start_response_writing(conn);
    }

    return PARSE_SUCCESS;
}

ParserState WebServer::determine_body_handling_state(Connection* conn) {
    log(LOG_TRACE,
        "WebServer::determine_body_handling_state called for client %d",
        conn->client_fd_);

    conn->parser_context_.clear_for_next_state();

    HttpRequest* request = conn->request_data_;
    if (request->method_ != "POST" && request->method_ != "PUT") {
        conn->request_data_->body_fully_parsed_ = true;
        return PARSER_COMPLETE;
    }

    std::string transfer_encoding = request->get_header("transfer-encoding");
    if (!transfer_encoding.empty() &&
        transfer_encoding.find("chunked") != std::string::npos) {
        return PARSER_READING_CHUNKED_BODY;
    }

    std::string content_length = request->get_header("content-length");
    if (!content_length.empty()) {
        char* end_ptr;
        request->content_length_ =
            std::strtoul(content_length.c_str(), &end_ptr, 10);
        conn->parser_context_.body_remaining_bytes_ = request->content_length_;

        if (request->content_length_ > 0) {
            return PARSER_READING_CONTENT_BODY;
        }
    }

    conn->request_data_->body_fully_parsed_ = true;
    return PARSER_COMPLETE;
}

ParseStatus WebServer::validate_version(Connection* conn) {
    log(LOG_TRACE, "WebServer::validate_version called for client %d",
        conn->client_fd_);

    std::string version = conn->request_data_->version_;

    // Only HTTP/1.0 or HTTP/1.1 allowed
    if (version == "HTTP/1.0" || version == "HTTP/1.1") {
        return PARSE_SUCCESS;
    }

    log(LOG_ERROR, "Invalid HTTP version '%s' in request for connection: %d",
        version.c_str(), conn->client_fd_);
    return PARSE_VERSION_NOT_SUPPORTED;
}

ParseStatus WebServer::validate_method(Connection* conn) {
    log(LOG_TRACE, "WebServer::validate_method called for client %d",
        conn->client_fd_);

    std::string method = conn->request_data_->method_;
    if (method != "GET" && method != "POST" && method != "DELETE") {
        log(LOG_ERROR, "Invalid HTTP method '%s' in request for connection: %d",
            method.c_str(), conn->client_fd_);
        return PARSE_METHOD_NOT_IMPLEMENTED;
    }

    // Check if the method is allowed for the matched location
    const Location* location = conn->location_match_;
    for (std::vector<std::string>::const_iterator it =
             location->allowed_methods_.begin();
         it != location->allowed_methods_.end(); ++it) {
        if (*it == method) {
            return PARSE_SUCCESS;
        }
    }
    log(LOG_ERROR, "Method '%s' not allowed for location: %s", method.c_str(),
        location->path_.c_str());

    std::string allowed_methods_str = join(location->allowed_methods_, ", ");
    conn->response_data_->set_error_header("Allow", allowed_methods_str);
    return PARSE_METHOD_NOT_ALLOWED;
}

ParseStatus WebServer::validate_body_handling(Connection* conn) {
    log(LOG_TRACE, "WebServer::validate_body_handling called for client %d",
        conn->client_fd_);

    HttpRequest* request = conn->request_data_;
    if (request->method_ == "POST" || request->method_ == "PUT") {
        bool has_content_length =
            !request->get_header("content-length").empty();
        bool has_transfer_encoding =
            !request->get_header("transfer-encoding").empty();

        if (!has_content_length && !has_transfer_encoding) {
            log(LOG_ERROR,
                "POST/PUT without Content-Length or Transfer-Encoding");
            return PARSE_MISSING_CONTENT_LENGTH;
        }

        if (has_content_length && has_transfer_encoding) {
            log(LOG_ERROR,
                "POST/PUT with both Content-Length and Transfer-Encoding");
            return PARSE_INVALID_CONTENT_LENGTH;
        }

        if (has_content_length) {
            std::string content_length = request->get_header("content-length");
            char* end_ptr;
            size_t body_size =
                std::strtoul(content_length.c_str(), &end_ptr, 10);

            if (end_ptr == content_length.c_str() || *end_ptr != '\0') {
                log(LOG_ERROR, "Invalid Content-Length header: '%s'",
                    content_length.c_str());
                return PARSE_INVALID_CONTENT_LENGTH;
            }

            if (static_cast<ssize_t>(body_size) >
                conn->location_match_->client_max_body_size_) {
                log(LOG_ERROR, "Content-Length exceeds maximum size: %zu",
                    body_size);
                return PARSE_CONTENT_TOO_LARGE;
            }
        }

        if (has_transfer_encoding) {
            std::string transfer_encoding =
                request->get_header("transfer-encoding");
            if (transfer_encoding != "chunked") {
                log(LOG_ERROR, "Unknown Transfer-Encoding: '%s'",
                    transfer_encoding.c_str());
                return PARSE_UNKNOWN_ENCODING;
            }
        }
    }
    log(LOG_DEBUG, "Body handling validation successful for connection: %d",
        conn->client_fd_);
    return PARSE_SUCCESS;
}

AHandler* WebServer::choose_handler(Connection* conn) {
    log(LOG_TRACE,
        "WebServer::choose_handler called for client %d, method %s, path "
        "%s",
        conn->client_fd_, conn->request_data_->method_.c_str(),
        conn->request_data_->path_.c_str());

    const Location* matching_location = conn->location_match_;
    const std::string& request_method = conn->request_data_->method_;
    const std::string& request_path = conn->request_data_->path_;

    if (matching_location->cgi_enabled_ && is_cgi_extension(request_path) &&
        request_method != "DELETE") {
        log(LOG_DEBUG,
            "choose_handler: Using CgiHandler for client_fd %d, path %s",
            conn->client_fd_, matching_location->path_.c_str());
        return &cgi_handler_;
    } else if (request_method == "POST") {
        log(LOG_DEBUG,
            "choose_handler: Using FileUploadHandler for client_fd %d, "
            "path %s",
            conn->client_fd_, matching_location->path_.c_str());
        return &file_upload_handler_;
    } else if (request_method == "DELETE") {
        log(LOG_DEBUG,
            "choose_handler: Using DeleteHandler for client_fd %d, path %s",
            conn->client_fd_, matching_location->path_.c_str());
        return &file_delete_handler_;
    } else {
        log(LOG_DEBUG,
            "choose_handler: Using StaticFileHandler for client_fd %d, "
            "path %s",
            conn->client_fd_, matching_location->path_.c_str());
        return &static_file_handler_;
    }
}

const Location* WebServer::match_location(const VirtualServer* vs,
                                          const std::string& path) const {
    log(LOG_TRACE,
        "WebServer::match_location called for path '%s' on virtual server "
        "'%s:%d'",
        path.c_str(), vs->host_.c_str(), vs->port_);

    const Location* best_prefix = NULL;
    const Location* best_extension = NULL;

    for (size_t i = 0; i < vs->locations_.size(); ++i) {
        const Location& loc = vs->locations_[i];
        if (loc.type_ == LOC_EXTENSION) {
            // Extension match: path ends with loc.path_
            if (path.length() >= loc.path_.length() &&
                path.compare(path.length() - loc.path_.length(),
                             loc.path_.length(), loc.path_) == 0) {
                if (!best_extension ||
                    loc.path_.length() > best_extension->path_.length()) {
                    best_extension = &loc;
                }
            }
        } else {
            // Prefix match: path starts with loc.path_
            if (path.find(loc.path_) == 0 &&
                (loc.path_ == "/" || path == loc.path_ ||
                 (path.length() > loc.path_.length() &&
                  (path[loc.path_.length()] == '/' ||
                   loc.path_[loc.path_.length() - 1] == '/')))) {
                if (!best_prefix ||
                    loc.path_.length() > best_prefix->path_.length()) {
                    best_prefix = &loc;
                }
            }
        }
    }

    // Prefer extension match over prefix match if both exist
    if (best_extension) {
        log(LOG_DEBUG,
            "Matched extension location '%s' for path '%s' on virtual server "
            "'%s:%d'",
            best_extension->path_.c_str(), path.c_str(), vs->host_.c_str(),
            vs->port_);
        return best_extension;
    }
    if (best_prefix) {
        log(LOG_DEBUG,
            "Matched prefix location '%s' for path '%s' on virtual server "
            "'%s:%d'",
            best_prefix->path_.c_str(), path.c_str(), vs->host_.c_str(),
            vs->port_);
        return best_prefix;
    }

    // Should never reach this because we enforce a '/' location at parsing
    log(LOG_FATAL,
        "No matching location found for path '%s' on virtual server '%s:%d'",
        path.c_str(), vs->host_.c_str(), vs->port_);
    return NULL;
}

void WebServer::match_host_header(Connection* conn) {
    if (!conn || !conn->request_data_ || !conn->default_virtual_server_) {
        log(LOG_FATAL, "match_host_header: Invalid connection or data.");
        return;
    }

    log(LOG_TRACE, "WebServer::match_host_header called for client %d",
        conn->client_fd_);

    // Get Host header value from the request
    std::string request_host_header_val =
        conn->request_data_->get_header("Host");
    std::string target_hostname = request_host_header_val;
    if (target_hostname.empty()) {
        conn->virtual_server_ = conn->default_virtual_server_;
        log(LOG_DEBUG, "No Host header. Using default virtual server for %s:%d",
            conn->virtual_server_->host_.c_str(), conn->virtual_server_->port_);
        return;
    }

    // Strip port number from Host header if present
    size_t colon_pos = target_hostname.find(':');
    if (colon_pos != std::string::npos) {
        target_hostname = target_hostname.substr(0, colon_pos);
    }

    // Loop through all virtual servers for this listener
    std::vector<VirtualServer*> vs_candidates =
        listener_to_virtual_servers_[conn->client_fd_];
    for (std::vector<VirtualServer*>::iterator it = vs_candidates.begin();
         it != vs_candidates.end(); ++it) {
        if ((*it)->host_ == target_hostname) {
            conn->virtual_server_ = *it;
            log(LOG_DEBUG, "Matched Host header '%s' to virtual server %s:%d",
                target_hostname.c_str(), (*it)->host_.c_str(), (*it)->port_);
            return;
        }
    }

    // If no match found, use the default virtual server
    conn->virtual_server_ = conn->default_virtual_server_;
    log(LOG_DEBUG,
        "No match for Host header '%s'. Using default virtual server %s:%d",
        target_hostname.c_str(), conn->virtual_server_->host_.c_str(),
        conn->virtual_server_->port_);
    return;
}

void WebServer::handle_cgi_read_event(IOContext* ctx, uint32_t event_flags) {
    Connection* conn = ctx->conn_;
    if (!conn || !conn->active_handler_) {
        log(LOG_FATAL,
            "handle_cgi_read_event: Connection or active handler is NULL");
        return;
    }

    log(LOG_TRACE,
        "WebServer::handle_cgi_read_event called with %s for cgi pipe fd %d",
        event_to_string(event_flags), ctx->fd_);

    if (event_flags & EPOLLIN) {
        Result result = cgi_handler_.handle_cgi_read(conn);
        if (!handle_async_result(result, conn, "handle_cgi_read_event")) {
            return;
        }

        if (!start_response_writing(conn)) {
            log(LOG_ERROR,
                "handle_cgi_read_event: Failed to start response writing "
                "for client_fd %d",
                conn->client_fd_);
            return;
        }
    } else {
        log(LOG_FATAL,
            "handle_cgi_read_event: Invalid event flags for CGI read event: %u",
            event_flags);
    }
}

void WebServer::handle_cgi_write_event(IOContext* ctx, uint32_t event_flags) {
    Connection* conn = ctx->conn_;
    if (!conn || !conn->active_handler_) {
        log(LOG_FATAL,
            "handle_cgi_write_event: Connection or active handler is NULL");
        return;
    }

    log(LOG_TRACE,
        "WebServer::handle_cgi_write_event called with %s for cgi pipe fd %d",
        event_to_string(event_flags), ctx->fd_);

    if (event_flags & EPOLLOUT) {
        Result result = cgi_handler_.handle_cgi_write(conn);
        if (!handle_async_result(result, conn, "handle_cgi_write_event")) {
            return;
        }
    } else {
        log(LOG_FATAL,
            "handle_cgi_write_event: Invalid event flags for CGI read event: "
            "%u",
            event_flags);
    }
}

bool WebServer::handle_async_result(Result result, Connection* conn,
                                    const char* context_str) {
    switch (result) {
        case AGAIN:
            log(LOG_DEBUG, "%s: Incomplete action, waiting.", context_str);
            return false;  // false means "stop processing"

        case ERROR:
            log(LOG_ERROR, "%s: Encountered an error.", context_str);
            conn->status_ = INTERNAL_SERVER_ERROR;
            handle_error_response(conn);
            return false;  // false means "stop processing"

        case COMPLETE:
            log(LOG_DEBUG, "%s: Finished succesfully.", context_str);
            return true;
    }

    log(LOG_FATAL, "%s: Unknown result from handler.", context_str);
    return false;
}

bool WebServer::start_response_writing(Connection* conn) {
    log(LOG_TRACE, "WebServer::start_response_writing called for client %d",
        conn->client_fd_);

    Result result = response_writer_.write_response_to_buffer(conn);
    if (result == ERROR) {
        log(LOG_TRACE,
            "start_response_writing: Write buffer is empty for client_fd %d. "
            "Response might be empty or complete.",
            conn->client_fd_);
        close_client_connection(conn);
        return false;
    }

    IOContext* client_ctx = conn->io_contexts_[FD_CLIENT_SOCKET];
    if (!update_context_in_epoll(client_ctx, EPOLLIN | EPOLLOUT)) {
        log(LOG_TRACE,
            "start_response_writing: Failed to update epoll events for "
            "client_fd %d",
            conn->client_fd_);
        close_client_connection(conn);
        return false;
    }

    conn->conn_state_ = CONN_WRITING_RESPONSE;
    log(LOG_DEBUG, "start_response_writing: Prepared client_fd %d for writing.",
        conn->client_fd_);
    return true;
}

void WebServer::handle_error_response(Connection* conn) {
    log(LOG_TRACE, "WebServer::handle_error_response called for client %d",
        conn->client_fd_);

    IOContext* client_ctx = conn->io_contexts_[FD_CLIENT_SOCKET];
    HttpStatus status = conn->status_;

    ErrorHandler::generate_error_response(conn, status);
    response_writer_.write_response_to_buffer(conn);
    if (!conn->write_buffer_.empty()) {
        if (!update_context_in_epoll(client_ctx, EPOLLIN | EPOLLOUT)) {
            log(LOG_ERROR,
                "handle_file_upload_event: Failed to update epoll events "
                "for client_fd %d",
                conn->client_fd_);
            close_client_connection(conn);
        }
        conn->conn_state_ = CONN_WRITING_RESPONSE;
    } else {
        log(LOG_DEBUG,
            "handle_error_response: No data to write for client_fd %d",
            conn->client_fd_);
    }
}
