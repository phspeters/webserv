#include "common.hpp"

WebServer* WebServer::instance_ = NULL;

WebServer::WebServer()
    : epoll_fd_(-1),
      ready_(false),
      request_parser_(NULL),
      response_writer_(NULL),
      static_file_handler_(NULL),
      cgi_handler_(NULL),
      file_upload_handler_(NULL),
      file_delete_handler_(NULL) {
    instance_ = this;
}

WebServer::~WebServer() {
    // Clean up owned components
    delete request_parser_;
    delete response_writer_;
    delete static_file_handler_;
    delete cgi_handler_;
    delete file_upload_handler_;
    delete file_delete_handler_;

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
    try {
        // Initialize components
        request_parser_ = new RequestParser();
        response_writer_ = new ResponseWriter();

        // Initialize handlers
        static_file_handler_ = new StaticFileHandler();
        cgi_handler_ = new CgiHandler();
        file_upload_handler_ = new FileUploadHandler();
        file_delete_handler_ = new FileDeleteHandler();
    } catch (const std::exception& e) {
        log(LOG_ERROR, "WebServer components memory allocation failed: %s",
            e.what());
        return false;
    }

    // Set up signal handlers
    if (!setup_signal_handlers()) {
        log(LOG_ERROR, "Failed to set up signal handlers");
        return false;
    }

    // Create epoll instance
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        log(LOG_ERROR, "Failed to create epoll instance");
        return false;
    }

    // Set up the listener sockets
    if (!setup_listener_sockets()) {
        return false;
    }

    log(LOG_INFO, "WebServer initialized successfully");

    return true;
}

bool WebServer::parse_config_file(const std::string& filename) {
    // Parse the configuration file and return a vector of VirtualServer objects

    // Check file extension
    std::string::size_type pos = filename.find_last_of(".");
    if (pos == std::string::npos || filename.substr(pos) != ".conf") {
        log(LOG_ERROR, "Error: Invalid configuration file extension: %s",
            filename.c_str());
        return false;  // Invalid file extension
    }

    // Open the configuration file
    std::ifstream file(filename.c_str());
    if (!file.is_open()) {
        log(LOG_ERROR, "Error: Could not open configuration file: %s",
            filename.c_str());
        return false;  // File open error
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Look for server block
        if (line == "server {" ||
            (line.find("server") == 0 && line.find("{") != std::string::npos)) {
            // Create a new virtual server
            VirtualServer virtual_server;
            // Parse the server block
            if (virtual_server.parse_server_block(file)) {
                if (!virtual_server.is_valid()) {
                    log(LOG_ERROR,
                        "Error: Invalid virtual server configuration");
                    return false;  // Validation error
                }

                log(LOG_DEBUG,
                    "Parsed valid virtual server configuration for host: %s, "
                    "port: %d",
                    virtual_server.host_.c_str(), virtual_server.port_);

                // Add to main list
                virtual_servers_.push_back(virtual_server);

                // Store pointer to the newly added server
                VirtualServer* server_ptr = &virtual_servers_.back();
            }

            log_virtual_server(LOG_TRACE, virtual_server);

        } else {
            log(LOG_ERROR, "Error parsing server block");
            return false;  // Parsing error
        }
    }

    // Close the file
    file.close();

    log(LOG_INFO, "Parsed %zu virtual servers from configuration file",
        virtual_servers_.size());
    return true;
}

void WebServer::run() {
    ready_ = true;

    // Start the event loop
    log(LOG_INFO, "WebServer is ready and waiting for connections");
    event_loop();
}

void WebServer::shutdown() {
    ready_ = false;
    log(LOG_INFO, "WebServer shutdown initiated");
}

bool WebServer::add_context_to_epoll(IOContext* ctx, uint32_t events) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.ptr = ctx;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, ctx->fd_, &event) < 0) {
        log(LOG_ERROR, "Failed to register socket '%i' on epoll", ctx->fd_);
        return false;
    }

    log(LOG_TRACE, "Registered socket '%i' on epoll with events %u", ctx->fd_,
        events);
    return true;
}

bool WebServer::update_context_in_epoll(IOContext* ctx, uint32_t events) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.ptr = ctx;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, ctx->fd_, &event) < 0) {
        log(LOG_ERROR, "Failed to up epoll events for socket '%i'", ctx->fd_);
        return false;
    }

    log(LOG_TRACE, "Updated epoll events for socket '%i' to %u", ctx->fd_,
        events);
    return true;
}

bool WebServer::remove_context_from_epoll(IOContext* ctx) {
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, ctx->fd_, NULL) < 0) {
        log(LOG_ERROR, "Failed to unregister socket '%i' on epoll", ctx->fd_);
        return false;
    }

    log(LOG_TRACE, "Unregistered socket '%i' on epoll", ctx->fd_);
    return true;
}

void WebServer::event_loop() {
    struct epoll_event events[MAX_EPOLL_EVENTS];

    while (ready_) {
        int timed_out = cleanup_timed_out_connections();
        if (timed_out > 0) {
            log(LOG_INFO, "Closed '%i' timed out connections.", timed_out);
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

            // Check for errors first. This applies to ALL context types.
            if (event_flags & (EPOLLERR | EPOLLHUP)) {
                log(LOG_ERROR, "Epoll error or hangup on fd %d (type: %d)",
                    ctx->fd_, ctx->type_);

                if (ctx->type_ == FD_LISTENER) {
                    remove_listener_context(ctx);
                } else {
                    close_client_connection(ctx->conn_);
                }
                // Skip further processing for this event.
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
                case FD_STATIC_FILE:
                    handle_static_file_event(ctx, event_flags);
                    break;
                case FD_CGI_PIPE_WRITE:
                    handle_cgi_write_event(ctx, event_flags);
                    break;
                case FD_CGI_PIPE_READ:
                    handle_cgi_read_event(ctx, event_flags);
                    break;
                case FD_FILE_UPLOAD:
                    handle_file_upload_event(ctx, event_flags);
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
        log(LOG_INFO, "Closed connection for client (fd: %i)", fd);
        active_connections_.erase(it);
    } else {
        log(LOG_FATAL, "Connection not found for socket '%i'", fd);
    }
}

bool WebServer::read_from_client_socket(Connection* conn) {
    log(LOG_DEBUG, "Reading from socket (fd: %i)", conn->client_fd_);

    ssize_t bytes_read = conn->read_buffer_.read_from(conn->client_fd_);

    if (bytes_read == 0) {
        log(LOG_WARNING, "Client disconnected (fd: %i)", conn->client_fd_);
        return false;
    }

    if (bytes_read == -1) {
        log(LOG_ERROR, "Error reading from socket (fd: %i): %s",
            conn->client_fd_, strerror(errno));
        return false;
    }

    if (bytes_read == BUFFER_FULL) {
        log(LOG_DEBUG, "Buffer full while reading from socket (fd: %i)",
            conn->client_fd_);
        return true;
    }

    conn->last_activity_ = time(NULL);

    log(LOG_DEBUG, "Read %zd bytes from socket (fd: %i)", bytes_read,
        conn->client_fd_);

    log_buffer(LOG_TRACE, conn->read_buffer_);

    return true;
}

bool WebServer::setup_listener_sockets() {
    std::map<std::pair<std::string, int>, int> listener_fds;

    for (std::list<VirtualServer>::iterator it = virtual_servers_.begin();
         it != virtual_servers_.end(); ++it) {
        // Get a pointer to the current VirtualServer object
        VirtualServer* vs = &(*it);
        std::pair<std::string, int> listen_addr(vs->host_, vs->port_);

        // If we haven't created a listener for this address yet, do it now.
        if (listener_fds.find(listen_addr) == listener_fds.end()) {
            int fd = create_listener_socket(vs->host_, vs->port_);
            if (fd < 0) {
                return false;  // Abort server startup
            }

            if (!add_listener_context(fd)) {
                return false;  // Abort server startup
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
    log(LOG_DEBUG, "Creating listener socket for host: %s on port: %i",
        host.c_str(), port);

    struct addrinfo filter, *results, *p;
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
    for (p = results; p != NULL; p = p->ai_next) {
        listener_fd = socket(p->ai_family, p->ai_socktype | SOCK_NONBLOCK,
                             p->ai_protocol);
        if (listener_fd < 0) {
            continue;  // Try next address
        }

        int opt = 1;
        if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &opt,
                       sizeof(opt)) < 0) {
            close(listener_fd);
            continue;  // Try next address
        }

        if (bind(listener_fd, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener_fd);
            continue;  // Try next address
        }

        break;  // Successfully bound
    }

    freeaddrinfo(results);  // Free the results linked list

    if (p == NULL) {
        log(LOG_ERROR, "Failed to bind to %s:%d", host.c_str(), port);
        return -1;
    }

    if (listen(listener_fd, SOMAXCONN) < 0) {
        close(listener_fd);
        log(LOG_ERROR, "Failed to listen on %s:%i", host.c_str(), port);
        return -1;
    }

    log(LOG_INFO, "Successfully created listener socket for %s:%i on fd %d",
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
            close_client_connection(conn);
            // Erase returns the next valid iterator
            it = active_connections_.erase(it);
            closed++;
        } else {
            ++it;
        }
    }

    return closed;
}

bool WebServer::set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        log(LOG_ERROR, "Failed to get flags for socket '%i'", fd);
        return false;
    }

    flags |= O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) == -1) {
        log(LOG_ERROR, "Failed to set non-blocking mode for socket '%i'", fd);
        return false;
    }

    return true;
}

bool WebServer::add_listener_context(int listener_fd) {
    if (listener_fd < 0) {
        log(LOG_FATAL, "add_listener_context: Invalid listener_fd '%i'",
            listener_fd);
        return false;
    }

    try {
        IOContext* ctx = new IOContext(listener_fd, FD_LISTENER, NULL);
        if (!add_context_to_epoll(ctx, EPOLLIN)) {
            log(LOG_ERROR, "Failed to add listener socket '%i' to epoll",
                ctx->fd_);
            close(ctx->fd_);
            delete ctx;
            return false;
        }

        listener_contexts_.push_back(ctx);
        log(LOG_INFO, "Listener socket '%i' added successfully", ctx->fd_);
        return true;
    } catch (const std::exception& e) {
        log(LOG_ERROR,
            "Failed to create IOContext for listener socket '%i': %s",
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
            "Listener socket '%i' not found in managed contexts during removal",
            ctx->fd_);
    }

    delete ctx;

    log(LOG_INFO, "Listener socket '%i' removed and cleaned up successfully",
        ctx->fd_);
    return found;  // Return true if it was a normal removal, false otherwise.
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

bool WebServer::is_cgi_extension(const std::string& request_uri) const {
    std::string extension = get_file_extension(request_uri);
    if (!extension.empty() &&
        (extension == ".php" || extension == ".py" || extension == ".sh")) {
        log(LOG_DEBUG, "Request uri: '%s' is a CGI script",
            request_uri.c_str());
        return true;
    }
    return false;
}

std::string WebServer::get_file_extension(const std::string& uri_path) const {
    size_t dot_pos = uri_path.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return "";  // No extension found
    }
    std::string extension = uri_path.substr(dot_pos);
    // Convert to lowercase for case-insensitive comparison
    for (std::string::iterator it = extension.begin(); it != extension.end();
         ++it) {
        *it = std::tolower(*it);
    }
    return extension;
}

void WebServer::accept_new_connection(int listener_fd) {
    log(LOG_DEBUG,
        "accept_new_connection: Processing new connection on listener_fd "
        "%d",
        listener_fd);

    // Find the default virtual server for this listener
    VirtualServer* default_server = NULL;
    if (listener_to_virtual_servers_.find(listener_fd) !=
        listener_to_virtual_servers_.end()) {
        default_server = listener_to_virtual_servers_[listener_fd].front();
    } else {
        log(LOG_FATAL, "No default server found for listener socket '%i'",
            listener_fd);
        return;
    }

    // Accept a new connection and set it to non-blocking mode
    int client_fd = accept4(listener_fd, NULL, NULL, SOCK_NONBLOCK);
    if (client_fd < 0) {
        log(LOG_ERROR, "Failed to accept new connection listener socket '%i'",
            listener_fd);
        return;
    }

    if (!create_client_connection(client_fd, default_server)) {
        return;
    }
}

Connection* WebServer::create_client_connection(
    int client_fd, const VirtualServer* default_virtual_server) {
    log(LOG_DEBUG, "Creating new connection for client_fd %d", client_fd);

    try {
        Connection* conn =
            new Connection(this, client_fd, default_virtual_server);

        active_connections_[client_fd] = conn;
        log(LOG_INFO, "Accepted new connection from client (fd: %i)",
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

    log(LOG_DEBUG,
        "handle_client_socket_event: Starting for client_fd %d with event %d",
        conn->client_fd_, event_flags);

    if (event_flags & EPOLLIN) {
        if (!read_from_client_socket(conn)) {
            log(LOG_ERROR,
                "handle_client_socket_event: Failed to read from socket for "
                "client_fd %d",
                conn->client_fd_);
            close_client_connection(conn);
            return;  // Error reading from socket, close connection
        }

        if (conn->conn_state_ == CONN_READING_REQUEST ||
            conn->conn_state_ == CONN_GENERATING_RESPONSE) {
            process_request_data(conn);
            log_request(LOG_TRACE, conn);
        }
    }

    if (event_flags & EPOLLOUT) {
        if (conn->conn_state_ == CONN_WRITING_RESPONSE ||
            conn->conn_state_ == CONN_FINISHING_WRITE) {
            ssize_t bytes_sent = conn->write_buffer_.write_to(conn->client_fd_);
            if (bytes_sent < 0) {
                log(LOG_ERROR,
                    "handle_client_socket_event: Error writing to socket for "
                    "client_fd %d: %s",
                    conn->client_fd_, strerror(errno));
                close_client_connection(conn);
                return;  // Error writing to socket, close connection
            }

            log(LOG_DEBUG,
                "handle_client_socket_event: Wrote %zd bytes to socket for "
                "client_fd %d",
                bytes_sent, conn->client_fd_);

            if (!conn->write_buffer_.empty()) {
                log(LOG_DEBUG,
                    "handle_client_socket_event: Incomplete write for "
                    "client_fd %d, remaining bytes: %zu",
                    conn->client_fd_, conn->write_buffer_.readable_bytes());
                return;  // Still data to write, wait for next EPOLLOUT event
            }

            // Switch back to reading state
            if (!update_context_in_epoll(ctx, EPOLLIN)) {
                log(LOG_ERROR,
                    "handle_client_socket_event: Failed to update epoll "
                    "events for client_fd %d",
                    conn->client_fd_);
                close_client_connection(conn);
                return;  // Error updating epoll, close connection
            }

            if (conn->conn_state_ == CONN_FINISHING_WRITE) {
                handle_keep_alive(conn);
            }

        } else {
            log(LOG_FATAL,
                "handle_event: Unexpected state for client_fd %d: %d",
                conn->client_fd_, conn->conn_state_);
            close_client_connection(conn);
        }
    }
}

bool WebServer::handle_keep_alive(Connection* conn) {
    log(LOG_DEBUG, "Handling keep-alive for client_fd %d", conn->client_fd_);

    // If the request was successful and keep-alive is enabled, reset the state
    if (conn->is_keep_alive()) {
        log(LOG_DEBUG, "Keep-alive enabled for client_fd %d", conn->client_fd_);
        conn->reset_for_keep_alive();
        return true;  // Keep-alive handled successfully
    } else {
        log(LOG_DEBUG, "Closing connection for client_fd %d", conn->client_fd_);
        close_client_connection(conn);
        return false;  // Connection closed
    }
}

// TODO: insert multiform data at the end of parse body functions
void WebServer::process_request_data(Connection* conn) {
    ParseStatus status = PARSE_SUCCESS;

    // Loop to process as much data as possible from the buffer in one go.
    while (true) {
        ParserState& state = conn->parser_context_.parser_state_;

        switch (state) {
            case PARSER_READING_REQUEST_LINE:
                status = request_parser_->parse_request_line(conn);
                if (status == PARSE_SUCCESS) {
                    state = PARSER_READING_HEADERS;
                }
                break;

            case PARSER_READING_HEADERS:
                status = request_parser_->parse_headers(conn);
                if (status == PARSE_SUCCESS) {
                    state = PARSER_PROCESSING_REQUEST;
                }
                break;

            case PARSER_PROCESSING_REQUEST:
                status = process_request(conn);  // Validate headers, etc.
                if (status == PARSE_SUCCESS) {
                    // Determine if we need to read a body
                    state = determine_body_handling_state(conn);
                }
                break;

            case PARSER_READING_CONTENT_BODY:
                status = request_parser_->parse_content_body(conn);
                if (status == PARSE_SUCCESS) {
                    state = PARSER_COMPLETE;
                }
                break;

            case PARSER_READING_CHUNKED_BODY:
                status = request_parser_->parse_chunked_body(conn);
                if (status == PARSE_SUCCESS) {
                    state = PARSER_COMPLETE;
                }
                break;

            case PARSER_DECODING_MULTIPART_BODY:
                status = request_parser_->parse_multipart_body(conn);
                if (status == PARSE_SUCCESS) {
                    state = PARSER_COMPLETE;
                }
                break;

            case PARSER_COMPLETE:
                log(LOG_DEBUG, "Request parsing complete for fd %d.",
                    conn->client_fd_);
                return;  // Exit the processing loop

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
            return;  // Exit and wait for the next EPOLLIN event.
        }

        if (status >= PARSE_ERROR) {
            log(LOG_ERROR, "Parse error %d for fd %d.", status,
                conn->client_fd_);
            ErrorHandler::generate_error_response(conn, status);
            // TODO: ResponseWriter try to serialize response to write_buffer_
            // TODO: Transition to CONN_WRITING_RESPONSE and set EPOLLOUT
            return;  // Exit the processing loop
        }
    }
}

// -----------------------------------------------------------------------------
// ------------------------- PROCESS REQUEST BUNDLE ----------------------------
// -----------------------------------------------------------------------------

// Dentro de handle_client_socket_event
ParseStatus WebServer::process_request(Connection* conn) {
    log(LOG_DEBUG, "Processing request for connection: %i", conn->client_fd_);
    // Phase 1: Estabilish context
    // a. Match host header with virtual server
    // b. Match best location block within virtual server
    // Phase 2: Validate request
    // c. Validate version (and host header for HTTP/1.1)
    // d. Validate request method is valid and allowed
    // e. Validate content length or transfer encoding: chunked
    // Phase 3: Choose handler and validate permissions
    // f. Choose handler based on request method and location
    // (choose_handler function) g. Validate permissions
    // (active_handler_->validate_permissions(conn))
    // Phase 4: Prepare for body handling and execution
    // h. Determine if body is needed and what kind

    // after that: parse_content_body (if needed) and setup_next_event_state

    // REFACTOR AND DELETE
    // Host header required for HTTP/1.1
    HttpRequest* request = conn->request_data_;
    if (request->version_ == "HTTP/1.1" &&
        request->get_header("host").empty()) {
        // Translates to response status 400
        log(LOG_ERROR,
            "Missing Host header in HTTP/1.1 request for connection: %i",
            conn->client_fd_);
        return PARSE_MISSING_HOST_HEADER;
    }

    if (request->method_ == "POST" || request->method_ == "PUT") {
        bool has_content_length =
            !request->get_header("content-length").empty();
        bool has_transfer_encoding =
            !request->get_header("transfer-encoding").empty();

        if (!has_content_length && !has_transfer_encoding) {
            // Translates to response status 411
            log(LOG_ERROR,
                "POST/PUT without Content-Length or Transfer-Encoding");
            return PARSE_MISSING_CONTENT_LENGTH;
        }

        if (has_content_length && has_transfer_encoding) {
            // Translates to response status 400
            log(LOG_ERROR,
                "POST/PUT with both Content-Length and Transfer-Encoding");
            return PARSE_INVALID_CONTENT_LENGTH;
        }

        if (has_content_length) {
            // Validate Content-Length
            std::string content_length = request->get_header("content-length");
            char* end_ptr;
            size_t body_size =
                std::strtoul(content_length.c_str(), &end_ptr, 10);

            // Check for invalid Content-Length format
            if (end_ptr == content_length.c_str() || *end_ptr != '\0') {
                log(LOG_ERROR, "Invalid Content-Length header: '%s'",
                    content_length.c_str());
                // Translates to response status 400
                return PARSE_INVALID_CONTENT_LENGTH;
            }

            if (body_size > conn->virtual_server_->client_max_body_size_) {
                log(LOG_ERROR, "Content-Length exceeds maximum size: %zu",
                    body_size);
                // Translates to response status 413
                return PARSE_CONTENT_TOO_LARGE;
            }
        }

        if (has_transfer_encoding) {
            // Validate Transfer-Encoding
            std::string transfer_encoding =
                request->get_header("transfer-encoding");
            if (transfer_encoding != "chunked") {
                log(LOG_ERROR, "Unknown Transfer-Encoding: '%s'",
                    transfer_encoding.c_str());
                // Translates to response status 501
                return PARSE_UNKNOWN_ENCODING;
            }
        }
    }

    log(LOG_DEBUG, "Headers validated successfully for connection: %i",
        conn->client_fd_);
    return PARSE_SUCCESS;
}  // REFACTOR AND DELETE

AHandler* WebServer::choose_handler(Connection* conn) {
    log(LOG_DEBUG,
        "choose_handler: Finding handler for client_fd %d, method %s, path "
        "%s",
        conn->client_fd_, conn->request_data_->method_.c_str(),
        conn->request_data_->path_.c_str());

    const Location* matching_location = conn->location_match_;
    const std::string& request_method = conn->request_data_->method_;
    const std::string& request_path = conn->request_data_->path_;

    // TODO - Change CGI condition to cgi_enabled + executable file + valid
    // script extension?
    // Return appropriate handler based on location config
    // CHECK AND TEST - Carol
    if (matching_location->cgi_enabled_ && is_cgi_extension(request_path) &&
        request_method != "DELETE") {
        // CGI handler for CGI-enabled locations
        log(LOG_DEBUG,
            "choose_handler: Using CgiHandler for client_fd %d, path %s",
            conn->client_fd_, matching_location->path_.c_str());
        conn->conn_state_ = CONN_GENERATING_RESPONSE;
        return cgi_handler_;
    } else if (request_method == "POST") {
        // FileUploadHandler for file uploads
        log(LOG_DEBUG,
            "choose_handler: Using FileUploadHandler for client_fd %d, "
            "path %s",
            conn->client_fd_, matching_location->path_.c_str());
        conn->conn_state_ = CONN_GENERATING_RESPONSE;
        return file_upload_handler_;
    } else if (request_method == "DELETE") {
        // DeleteHandler for dlete requests
        log(LOG_DEBUG,
            "choose_handler: Using DeleteHandler for client_fd %d, path %s",
            conn->client_fd_, matching_location->path_.c_str());
        conn->conn_state_ = CONN_GENERATING_RESPONSE;
        return file_delete_handler_;
    } else {
        // Default to StaticFileHandler for regular files
        log(LOG_DEBUG,
            "choose_handler: Using StaticFileHandler for client_fd %d, "
            "path %s",
            conn->client_fd_, matching_location->path_.c_str());
        conn->conn_state_ = CONN_GENERATING_RESPONSE;
        return static_file_handler_;
    }
}

ParserState WebServer::determine_body_handling_state(Connection* conn) {
    HttpRequest* request = conn->request_data_;
    // Check for request body
    if (request->method_ == "POST" || request->method_ == "PUT") {
        // Check for Transfer-Encoding header
        std::string transfer_encoding =
            request->get_header("transfer-encoding");
        if (!transfer_encoding.empty() &&
            transfer_encoding.find("chunked") != std::string::npos) {
            return PARSER_READING_CONTENT_BODY;
        }

        // Check for Content-Length header
        std::string content_length = request->get_header("content-length");
        if (!content_length.empty()) {
            char* end_ptr;
            size_t body_size =
                std::strtoul(content_length.c_str(), &end_ptr, 10);

            if (body_size > 0) {
                return PARSER_READING_CHUNKED_BODY;
            }
        }
    }

    // No body needed or zero-length body
    return PARSER_COMPLETE;
}

// Add Location Type Check
const Location* WebServer::find_matching_location(
    const VirtualServer* virtual_server, const std::string& uri) const {
    // Use a reference instead of making a copy
    const std::vector<Location>& locations_ = virtual_server->locations_;
    const Location* best_match = NULL;

    for (std::vector<Location>::const_iterator it = locations_.begin();
         it != locations_.end(); ++it) {
        const Location& location = *it;
        // Check if the request path starts with the location path
        if (uri.find(location.path_) == 0) {
            // Make sure we match complete segments
            if (location.path_ == "/" ||  // Root always matches
                uri == location.path_ ||  // Exact match
                (uri.length() > location.path_.length() &&
                 (uri[location.path_.length()] == '/' ||
                  location.path_[location.path_.length() - 1] == '/'))) {
                if (!best_match ||
                    location.path_.length() > best_match->path_.length()) {
                    best_match = &location;
                }
            }
        }
    }

    if (!best_match) {
        log(LOG_FATAL, "No matching location found for URI: %s", uri.c_str());
    } else {
        log(LOG_DEBUG, "Found matching location: %s",
            best_match->path_.c_str());

        std::string allowed_methods_str = "";
        for (size_t i = 0; i < best_match->allowed_methods_.size(); i++) {
            if (i > 0) {
                allowed_methods_str += ", ";
            }
            allowed_methods_str += best_match->allowed_methods_[i];
        }

        log(LOG_TRACE,
            "Location details: path=%s, root=%s, autoindex=%d, "
            "cgi_enabled=%d, "
            "allowed_methods=%s, index=%s, redirect=%s",
            best_match->path_.c_str(), best_match->root_.c_str(),
            best_match->autoindex_, best_match->cgi_enabled_,
            allowed_methods_str.c_str(), best_match->index_.c_str(),
            best_match->redirect_.c_str());
    }

    return best_match;
}

// ------------- TODO: fix functions below -----------

bool WebServer::validate_request_location(Connection* conn) {
    const Location* matching_location = conn->location_match_;
    if (!matching_location) {
        log(LOG_ERROR, "No matching location found for request path: %s",
            conn->request_data_->path_.c_str());
        ErrorHandler::generate_error_response(conn, NOT_FOUND);
        return false;
    }

    const std::string& request_method = conn->request_data_->method_;

    // Check if the requested method is allowed for this location
    if (!matching_location->allowed_methods_.empty()) {
        bool method_allowed = false;
        std::string allowed_methods_str;

        for (size_t i = 0; i < matching_location->allowed_methods_.size();
             i++) {
            // Build Allow header value
            if (i > 0) {
                allowed_methods_str += ", ";
            }
            allowed_methods_str += matching_location->allowed_methods_[i];

            // Check if the current request method is allowed
            if (matching_location->allowed_methods_[i] == request_method) {
                method_allowed = true;
            }
        }

        if (!method_allowed) {
            log(LOG_DEBUG,
                "Connection '%i', Host '%s': Method not allowed: %s, "
                "Allowed "
                "methods: %s",
                conn->client_fd_, conn->virtual_server_->host_name_.c_str(),
                request_method.c_str(), allowed_methods_str.c_str());

            // Apply 405 error directly to the response
            ErrorHandler::generate_error_response(conn, METHOD_NOT_ALLOWED);
            log(LOG_WARNING,
                "validate_request_location: Invalid request location for "
                "client_fd %d",
                conn->client_fd_);

            // Add the Allow header
            conn->response_data_->set_header("Allow", allowed_methods_str);

            return false;
        }
    }

    log(LOG_DEBUG,
        "Connection '%i', Host '%s': Request method '%s' is allowed for "
        "path "
        "'%s'",
        conn->client_fd_, conn->virtual_server_->host_name_.c_str(),
        request_method.c_str(), matching_location->path_.c_str());
    return true;
}

// TODO: improve this ugly ass function
void WebServer::match_host_header(Connection* conn) {
    if (!conn || !conn->request_data_ || !conn->default_virtual_server_) {
        log(LOG_FATAL, "match_host_header: Invalid connection or data.");
        // conn->virtual_server_ should already be default_virtual_server_
        // or NULL if creation failed
        return;
    }

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

    // The connection's default_virtual_server_ tells us the port and listen
    // IP this connection is associated with.
    int listener_port = conn->default_virtual_server_->port_;
    std::string listener_host_ip =
        conn->default_virtual_server_->host_;  // IP from 'listen' directive

    VirtualServer* matched_vs = NULL;

    // 1. Check servers listening on the specific IP:Port of the connection
    std::map<int, std::map<std::string, std::vector<VirtualServer*> > >::
        const_iterator port_it = port_to_hosts_.find(listener_port);
    if (port_it != port_to_hosts_.end()) {
        const std::map<std::string, std::vector<VirtualServer*> >&
            hosts_on_port = port_it->second;

        // Check specific listener IP
        std::map<std::string, std::vector<VirtualServer*> >::const_iterator
            host_ip_it = hosts_on_port.find(listener_host_ip);
        if (host_ip_it != hosts_on_port.end()) {
            const std::vector<VirtualServer*>& candidate_servers =
                host_ip_it->second;
            for (std::vector<VirtualServer*>::const_iterator server_it =
                     candidate_servers.begin();
                 server_it != candidate_servers.end(); ++server_it) {
                VirtualServer* vs = *server_it;
                for (std::vector<std::string>::const_iterator name_it =
                         vs->server_names_.begin();
                     name_it != vs->server_names_.end(); ++name_it) {
                    if (*name_it == target_hostname) {
                        matched_vs = vs;
                        break;  // Found specific server_name match on
                                // specific IP
                    }
                }
                if (matched_vs) {
                    break;
                }  // Break if we found a match
            }
        }

        // 2. If no match on specific IP, check servers listening on 0.0.0.0
        // (wildcard) for the same port
        if (!matched_vs && listener_host_ip != "0.0.0.0") {
            std::map<std::string, std::vector<VirtualServer*> >::const_iterator
                wildcard_host_ip_it = hosts_on_port.find("0.0.0.0");
            if (wildcard_host_ip_it != hosts_on_port.end()) {
                const std::vector<VirtualServer*>& candidate_wildcard_servers =
                    wildcard_host_ip_it->second;
                for (std::vector<VirtualServer*>::const_iterator server_it =
                         candidate_wildcard_servers.begin();
                     server_it != candidate_wildcard_servers.end();
                     ++server_it) {
                    VirtualServer* vs = *server_it;
                    for (std::vector<std::string>::const_iterator name_it =
                             vs->server_names_.begin();
                         name_it != vs->server_names_.end(); ++name_it) {
                        if (*name_it == target_hostname) {
                            matched_vs = vs;
                            break;  // Found specific server_name match on
                                    // wildcard IP
                        }
                    }
                    if (matched_vs) {
                        break;
                    }
                }
            }
        }
    }

    if (matched_vs) {
        log(LOG_DEBUG,
            "Matched Host header '%s' to virtual server with primary name "
            "'%s' "
            "on %s:%d",
            request_host_header_val.c_str(),
            matched_vs->server_names_.empty()
                ? matched_vs->host_name_.c_str()
                : matched_vs->server_names_[0].c_str(),
            matched_vs->host_.c_str(), matched_vs->port_);
        conn->virtual_server_ = matched_vs;
    } else {
        // No specific server_name match found, conn->virtual_server_
        // remains as conn->default_virtual_server_.
        log(LOG_DEBUG,
            "No specific virtual server for Host header '%s'. Using "
            "default "
            "for listener %s:%d (primary name '%s').",
            request_host_header_val.c_str(),
            conn->virtual_server_->host_.c_str(), conn->virtual_server_->port_,
            conn->virtual_server_->server_names_.empty()
                ? conn->virtual_server_->host_name_.c_str()
                : conn->virtual_server_->server_names_[0].c_str());
    }
}

void WebServer::handle_file_upload_event(IOContext* ctx, uint32_t event_flags) {
    Connection* conn = ctx->conn_;
    if (conn && conn->active_handler_) {
        conn->active_handler_->handle_event(conn);
    }
}
