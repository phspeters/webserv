#include "webserv.hpp"

WebServer* WebServer::instance_ = NULL;

WebServer::WebServer()
    : epoll_fd_(-1),
      ready_(false),
      conn_manager_(NULL),
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
    delete conn_manager_;
    delete request_parser_;
    delete response_writer_;
    delete static_file_handler_;
    delete cgi_handler_;
    delete file_upload_handler_;
    delete file_delete_handler_;

    // Close listener sockets if they are open
    for (std::vector<int>::iterator it = listener_fds_.begin();
         it != listener_fds_.end(); ++it) {
        if (*it != -1) {
            log(LOG_TRACE, "Closing listener socket: %d", *it);
            close(*it);
        }
    }

    if (epoll_fd_ >= 0) {
        log(LOG_TRACE, "Closing epoll instance: %d", epoll_fd_);
        close(epoll_fd_);
    }

    log(LOG_INFO, "WebServer resources cleaned up");
}

bool WebServer::init() {
    try {
        // Initialize components
        conn_manager_ = new ConnectionManager();
        request_parser_ = new RequestParser();
        response_writer_ = new ResponseWriter();

        // Initialize handlers
        static_file_handler_ = new StaticFileHandler();
        cgi_handler_ = new CgiHandler();
        file_upload_handler_ = new FileUploadHandler();
        file_delete_handler_ = new FileDeleteHandler();
    } catch (const std::bad_alloc& e) {
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
                std::string error_msg;
                if (!virtual_server.is_valid()) {
                    log(LOG_ERROR,
                        "Error: Invalid virtual server configuration");
                    return false;  // Validation error
                }

                log(LOG_DEBUG,
                    "Parsed valid virtual server configuration for host: %s, "
                    "port: %d",
                    virtual_server.host_.c_str(), virtual_server.port_);

                // Add to main vector
                virtual_servers_.push_back(virtual_server);

                // Store pointer to the newly added server (vector might
                // reallocate)
                VirtualServer* server_ptr = &virtual_servers_.back();

                // Group by port first, then by host
                int port = server_ptr->port_;
                std::string host = server_ptr->host_;
                port_to_hosts_[port][host].push_back(server_ptr);
                log(LOG_DEBUG,
                    "Added virtual server for port %d, host '%s' to "
                    "port_to_hosts_",
                    port, host.c_str());
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

void WebServer::event_loop() {
    struct epoll_event events[MAX_EPOLL_EVENTS];

    while (ready_) {
        int timed_out = cleanup_timed_out_connections();
        if (timed_out > 0) {
            log(LOG_INFO, "Closed '%i' timed out connections.", timed_out);
        }

        // DEBATE: Create IOContext for each fd/socket and return it from epoll
        // Based on fd type, we call a specific handle function
        /* e.g.
		int ready_events = epoll_wait(epoll_fd_, events, MAX_EPOLL_EVENTS,
									  http_limits::TIMEOUT);
        for (int i = 0; i < ready_events; i++) {
        // The pointer gives you ALL the context you need.
        IOContext* ctx = static_cast<IOContext*>(events[i].data.ptr);
        uint32_t event_flags = events[i].events;

        // Dispatch based on the context type, not the FD number.
        switch (ctx->type) {
            case IOContext::LISTENER_SOCKET:
                accept_new_connection(ctx->fd);
                break;
            case IOContext::CLIENT_SOCKET:
                handle_client_socket_event(ctx->conn, event_flags);
				-- Se EPOLLIN = read_from_socket
				---- Se state == codes::READING_HEADERS || PROCESSING_BODY = parse_request
				------- Se PARSE_SUCCESS = setup_next_event_state
				-- Se EPOLLOUT = write_to_socket (state WRITING_RESPONSE required? Deixar um else de log de aviso que algo está errado?) e limpa o que foi escrito
                break;
            case IOContext::STATIC_FILE:
			(Atencao especial ao FileDelete que já executará após check_permissions ou similar)
                handle_static_file_event(ctx->conn, event_flags);
				-- Sempre EPOLLIN
				-- Se state GENERATING_RESPONSE -> handler prepara a response (response line, headers e fd do static file)
				---- Se sucesso, troca state para WRITING_RESPONSE
				-- Se state WRITING_RESPONSE -> Response writer escreve a response no write_buffer e troca client socket para EPOLLOUT
                break;
			case IOContext::CGI_PIPE_WRITE:
				handle_cgi_write_event(ctx->conn, event_flags);
				(ONDE SERÁ O SETUP DO CGI??)
				(ONDE SERÁ O FORK??)
				-- Se EPOLLOUT = write body to pipe
				break;
            case IOContext::CGI_PIPE_READ:
			(VERIFICAR FORMA DE SABER SE O SCRIPT ESTÁ FUNCIONANDO ANTES DE TENTAR LER)
                handle_cgi_read_event(ctx->conn, event_flags);
				-- Sempre EPOLLIN
				-- Se state GENERATING_RESPONSE -> handler prepara a response (request line, headers e fd do cgi_read_pipe)
				---- Se sucesso, troca state para WRITING_RESPONSE
				-- Se state WRITING_RESPONSE -> Response writer escreve a response no write_buffer e troca client socket para EPOLLOUT
                break;
			case IOContext::FILE_UPLOAD:
				handle_file_upload_event(ctx->conn, event_flags);
				-- Sempre EPOLLOUT
				-- Se state GENERATING_RESPONSE -> handler prepara a response (request line, headers e fd do file_upload)
				---- Se sucesso, troca state para WRITING_RESPONSE
				-- Se state WRITING_RESPONSE -> Response writer escreve a request line e headers no write_buffer, depois le direto do file_upload_buffer o que falta e troca client socket para EPOLLOUT
				break;
            // ... etc
        }*/

        // Wait for events on the epoll instance
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
            log(LOG_DEBUG, "event_loop: Processing %d ready events",
                ready_events);
        }

        for (int i = 0; i < ready_events; i++) {
            int fd = events[i].data.fd;
            uint32_t event_flags = events[i].events;

            // Check if this is a listener socket
            bool is_listener = false;
            for (std::vector<int>::iterator it = listener_fds_.begin();
                 it != listener_fds_.end(); ++it) {
                if (fd == *it) {
                    is_listener = true;
                    break;
                }
            }

            if (is_listener) {
                if (event_flags & (EPOLLERR | EPOLLHUP)) {
                    // Handle errors on listener sockets
                    log(LOG_ERROR, "Error on listener socket %i: %s", fd,
                        strerror(errno));
                    remove_listener_socket(fd);
                    continue;
                }
                // Accept new connection on listener socket
                log(LOG_INFO, "New connection on socket '%i'", fd);
                accept_new_connection(fd);
            } else {
                // Handle connection socket event
                log(LOG_INFO, "Connection event on socket '%i'", fd);
                handle_connection_event(fd, event_flags);
            }
        }
    }

    log(LOG_INFO, "event_loop: Server event loop terminated");
}

void WebServer::accept_new_connection(int listener_fd) {
    log(LOG_DEBUG,
        "accept_new_connection: Processing new connection on listener_fd %d",
        listener_fd);

    // Find the default virtual server for this listener
    VirtualServer* default_server = NULL;
    if (listener_to_default_server_.find(listener_fd) !=
        listener_to_default_server_.end()) {
        default_server = listener_to_default_server_[listener_fd];
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

    log(LOG_DEBUG,
        "accept_new_connection: Accepted new client_fd %d from listener_fd %d",
        client_fd, listener_fd);

    // Register with epoll for read events
    if (!register_epoll_events(client_fd)) {
        close(client_fd);
        return;
    }

    // Create connection with the default virtual server
    Connection* conn =
        conn_manager_->create_connection(client_fd, default_server);
    if (!conn) {
        close(client_fd);
    }
}

void WebServer::handle_connection_event(int client_fd, uint32_t events) {
    Connection* conn = conn_manager_->get_connection(client_fd);
    if (!conn) {
        return;
    }

    if (events & (EPOLLERR | EPOLLHUP)) {
        log(LOG_ERROR,
            "handle_connection_event: Error or hangup on client_fd %d, events: "
            "%u",
            client_fd, events);
        handle_error(conn);
    } else {
        handle_event(conn);
    }

	/*

	    IOContext* ctx = (IOContext*)event.data.ptr;
    switch (ctx->type) {
        case IOContext::CLIENT_SOCKET:
            handle_client_socket_event(ctx->conn, events);
            break;
        case IOContext::STATIC_FILE:
            handle_static_file_event(ctx->conn, events);
            break;
        // ... etc.
    }
	*/
}

void WebServer::close_client_connection(Connection* conn) {
    if (!conn) {
        log(LOG_FATAL, "Connection is invalid, cannot close.");
        return;
    }

    // First unregister from epoll (must happen before socket closure)
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, conn->client_fd_, NULL) < 0) {
        log(LOG_ERROR, "Failed to unregister socket %i from epoll",
            conn->client_fd_);
        return;
    }

    log(LOG_DEBUG, "close_client_connection: Closing client_fd %d",
        conn->client_fd_);

    // Then let connection manager handle the rest
    conn_manager_->close_connection(conn);
}

void WebServer::handle_event(Connection* conn) {
    log(LOG_DEBUG, "handle_event: Starting for client_fd %d", conn->client_fd_);

    if (!conn) {
        log(LOG_FATAL, "handle_event: Connection pointer is NULL");
        return;
    }

	// Start of handle_client_socket_event
    if (conn->conn_state_ >= codes::PARSING_REQUEST_LINE ||
        conn->conn_state_ <= codes::PARSING_CHUNKED_BODY) {
        if (!request_parser_->read_from_socket(conn)) {
            log(LOG_ERROR,
                "handle_event: Failed to read from socket for client_fd %d",
                conn->client_fd_);
            handle_error(conn);
            return;
        }
    }

    codes::ParseStatus parse_status = codes::PARSE_SUCCESS;
    if (conn->conn_state_ == codes::PARSING_REQUEST_LINE) {
        parse_status = request_parser_->parse_request_line(conn);
        if (parse_status == codes::PARSE_INCOMPLETE) {
            log(LOG_DEBUG,
                "handle_event: Incomplete request line for client_fd %d, "
                "waiting for more data",
                conn->client_fd_);
            return;  // Need more data to complete request line
        }
        if (parse_status == codes::PARSE_SUCCESS) {
            conn->conn_state_ = codes::PARSING_HEADERS;
            conn->parser_context_.parser_state_ = 0;
        }
    }

    if (conn->conn_state_ == codes::PARSING_HEADERS) {
        parse_status = request_parser_->parse_headers(conn);
        if (parse_status == codes::PARSE_INCOMPLETE) {
            log(LOG_DEBUG,
                "handle_event: Incomplete headers for client_fd %d, waiting "
                "for "
                "more data",
                conn->client_fd_);
            return;  // Need more data to complete headers
        }
        if (parse_status == codes::PARSE_SUCCESS) {
            conn->conn_state_ = codes::PROCESSING_REQUEST;
            conn->parser_context_.parser_state_ = 0;
        }
    }

    if (conn->conn_state_ == codes::PROCESSING_REQUEST) {
        // Process the request after headers are parsed
        parse_status = process_request(conn);
        if (parse_status == codes::PARSE_SUCCESS) {
            // Determine if we need to handle a body and if so, what kind
            conn->conn_state_ =
                determine_body_handling_state(conn);  // TODO reset context
        }
    }

    if (conn->conn_state_ == codes::PARSING_BODY) {
        log(LOG_DEBUG, "handle_event: Reading body for client_fd %d",
            conn->client_fd_);
        parse_status = request_parser_->parse_body(conn);
        if (parse_status == codes::PARSE_INCOMPLETE) {
            log(LOG_DEBUG,
                "handle_event: Incomplete body for client_fd %d, waiting for "
                "more data",
                conn->client_fd_);
            return;  // Need more data to complete body
        }
        log(LOG_DEBUG,
            "handle_event: Body parsed successfully for client_fd %d",
            conn->client_fd_);
    }

    if (conn->conn_state_ == codes::PARSING_CHUNKED_BODY) {
        log(LOG_DEBUG, "handle_event: Reading chunked body for client_fd %d",
            conn->client_fd_);
        parse_status = request_parser_->parse_chunked_body(conn);
        if (parse_status == codes::PARSE_INCOMPLETE) {
            log(LOG_DEBUG,
                "handle_event: Incomplete chunked body for client_fd %d, "
                "waiting for more data",
                conn->client_fd_);
            return;  // Need more data to complete chunked body
        }
        log(LOG_DEBUG,
            "handle_event: Chunked body parsed successfully for client_fd %d",
            conn->client_fd_);
    }

    log_request(LOG_TRACE, conn);

    if (parse_status >= codes::PARSE_ERROR) {
        log(LOG_ERROR,
            "handle_event: Error processing request for client_fd %d, "
            "status: %d",
            conn->client_fd_, parse_status);
        ErrorHandler::generate_error_response(conn, parse_status);
        // Connection state already set to WRITING_RESPONSE
    }

    // If we reach here, the request has been successfully parsed and processed
    if (conn->conn_state_ <= codes::PARSING_CHUNKED_BODY) {
        log(LOG_DEBUG,
            "handle_event: Request processing complete for client_fd %d, "
            "moving to response generation",
            conn->client_fd_);
    }

    //--- <= codes::PARSING_CHUNKED_BODY
    //--- split here into two functions? Decide which to call based on state ---
    //--- >= codes::GENERATING_RESPONSE

    if (conn->conn_state_ == codes::GENERATING_RESPONSE ||
        conn->conn_state_ == codes::EXECUTING_CGI) {
        // Route the request to the appropriate handler
        // DEBATE: nginx calls this and all the validations needed for the
        // handler before commiting to reading the body
        if (!conn->active_handler_) {
            log(LOG_DEBUG,
                "handle_write: conn->active_handler_ IS NULL. Validating "
                "request location for client_fd %d.",
                conn->client_fd_);
            conn->active_handler_ = choose_handler(conn);
        }
		// Handler part sould move to their respective handle event function
        // Call the handler to process the request and generate a response
        if (conn->active_handler_) {
            conn->active_handler_->handle(conn);
        }
    }

    if (conn->conn_state_ == codes::WRITING_RESPONSE) {
        // Write the response to the client
        codes::WriteStatus status = response_writer_->write_response(conn);

        // TODO - Remove switch state, call logs and handle_error before
        // returning the status. Leave condition if(status ==
        // codes::WRITE_INCOMPLETE) { return; } because if it is incomplete,
        // we should return and wait for the next call, and if it is an writing
        // error, the connection should be closed inse write_response, never
        // reaching this point. Only WRITE_SUCCESS will continue the flow.
        switch (status) {
            case codes::WRITE_INCOMPLETE:
                log(LOG_DEBUG,
                    "handle_write: Response writing incomplete for client_fd "
                    "%d, "
                    "will resume later",
                    conn->client_fd_);
                update_epoll_events(conn->client_fd_, EPOLLOUT);
                return;
            case codes::WRITE_ERROR:
                log(LOG_ERROR,
                    "handle_write: Error writing response to client_fd %d",
                    conn->client_fd_);
                handle_error(conn);
                return;
            case codes::WRITE_SUCCESS:
                log(LOG_DEBUG,
                    "handle_write: Response completely written to client_fd %d",
                    conn->client_fd_);
                break;
        }

        // TODO - Wrap the code below in a function e.g. handle_keep_alive

        // Check for error status codes that should close the connection
        int status_code = conn->response_data_->status_code_;
        log(LOG_DEBUG, "handle_write: Response status code %d for client_fd %d",
            status_code, conn->client_fd_);

        // ADDED: Check if response explicitly sets connection: close
        std::string response_connection =
            conn->response_data_->get_header("connection");
        bool should_close = false;

        if (response_connection == "close") {
            should_close = true;
            log(LOG_DEBUG,
                "handle_write: Response sets connection: close for client_fd "
                "%d",
                conn->client_fd_);
        } else if (status_code == 400 || status_code == 413 ||
                   status_code >= 500) {
            // ADDED: Close connections for client and server errors
            should_close = true;
            log(LOG_INFO,
                "handle_write: Closing connection for error status %d on "
                "client_fd %d",
                status_code, conn->client_fd_);
        }

        // ADDED: Proper connection handling logic
        if (should_close) {
            log(LOG_DEBUG, "handle_write: Closing connection for client_fd %d",
                conn->client_fd_);
            close_client_connection(conn);
        } else if (conn->is_keep_alive()) {
            log(LOG_DEBUG,
                "handle_write: Keep-alive enabled, resetting connection for "
                "client_fd %d",
                conn->client_fd_);
            conn->reset_for_keep_alive();
            update_epoll_events(conn->client_fd_, EPOLLIN);
        } else {
            log(LOG_DEBUG,
                "handle_write: No keep-alive, closing connection for client_fd "
                "%d",
                conn->client_fd_);
            close_client_connection(conn);
        }
    }
}

void WebServer::handle_error(Connection* conn) {
    log(LOG_ERROR, "handle_error: Handling error for client_fd %d",
        conn->client_fd_);
    close_client_connection(conn);
}

bool WebServer::setup_listener_sockets() {
    for (std::map<int, std::map<std::string, std::vector<VirtualServer*> > >::
             iterator it = port_to_hosts_.begin();
         it != port_to_hosts_.end(); ++it) {
        int port = it->first;
        std::map<std::string, std::vector<VirtualServer*> >& hosts = it->second;
        // If we have a wildcard for this port, only create one socket
        bool has_wildcard = (hosts.find("0.0.0.0") != hosts.end());

        if (has_wildcard) {
            // Just bind to 0.0.0.0
            if (!create_listener_socket("0.0.0.0", port, hosts)) {
                return false;
            }
        } else {
            // Create one socket per specific host
            for (std::map<std::string, std::vector<VirtualServer*> >::iterator
                     host_it = hosts.begin();
                 host_it != hosts.end(); ++host_it) {
                if (!create_listener_socket(host_it->first, port, hosts)) {
                    return false;
                }
            }
        }
    }

    return true;
}

bool WebServer::create_listener_socket(
    const std::string& host, int port,
    std::map<std::string, std::vector<VirtualServer*> >& hosts) {
    log(LOG_DEBUG, "Creating listener socket for host: %s on port: %i",
        host.c_str(), port);

    int listener_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listener_fd < 0) {
        log(LOG_ERROR, "Failed to create listener socket on port: %i", port);
        return false;
    }

    // Set SO_REUSEADDR option
    int opt = 1;
    if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
        0) {
        log(LOG_ERROR, "Failed to set socket options for %s:%i", host.c_str(),
            port);
        close(listener_fd);
        return false;
    }

    // Bind to specified host:port
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        addr.sin_addr.s_addr = inet_addr(host.c_str());
        if (addr.sin_addr.s_addr == INADDR_NONE) {
            log(LOG_ERROR, "Invalid IP address: %s", host.c_str());
            close(listener_fd);
            return false;
        }
    }

    if (bind(listener_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log(LOG_ERROR, "Failed to bind to %s:%i: %s", host.c_str(), port,
            strerror(errno));
        close(listener_fd);
        return false;
    }

    if (listen(listener_fd, SOMAXCONN) < 0) {
        log(LOG_ERROR, "Failed to listen on %s:%i", host.c_str(), port);
        close(listener_fd);
        return false;
    }

    // Register with epoll
    if (!register_epoll_events(listener_fd)) {
        log(LOG_ERROR, "Failed to register %s:%i with epoll", host.c_str(),
            port);
        close(listener_fd);
        return false;
    }

    // Save the listener FD and map to default server for this host:port
    listener_fds_.push_back(listener_fd);
    if (hosts.find(host) != hosts.end() && !hosts[host].empty()) {
        listener_to_default_server_[listener_fd] =
            hosts[host][0];  // First server is default
    }

    log(LOG_INFO, "Created socket for %s:%i", host.c_str(), port);
    return true;
}

int WebServer::cleanup_timed_out_connections() {
    return conn_manager_->close_timed_out_connections();
}

void WebServer::remove_listener_socket(int fd) {
    // First, unregister from epoll
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, NULL) < 0) {
        log(LOG_ERROR, "Failed to remove listener '%i' from epoll: %s", fd,
            strerror(errno));
        // Continue anyway to clean up our internal structures
    }

    // Remove from listener_to_default_server_ map
    listener_to_default_server_.erase(fd);

    // Remove from listener_fds_ vector
    for (std::vector<int>::iterator it = listener_fds_.begin();
         it != listener_fds_.end(); ++it) {
        if (*it == fd) {
            listener_fds_.erase(it);
            break;
        }
    }

    // Close the socket
    close(fd);

    log(LOG_DEBUG, "Removed faulty listener socket '%i'", fd);
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

bool WebServer::register_epoll_events(int fd, uint32_t events) {
    WebServer* server = get_instance();
    if (!server) {
        log(LOG_FATAL,
            "WebServer instance is NULL, cannot register epoll events");
        return false;
    }

    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.fd = fd;

    if (epoll_ctl(server->epoll_fd_, EPOLL_CTL_ADD, fd, &event) < 0) {
        log(LOG_ERROR, "Failed to register socket '%i' on epoll", fd);
        return false;
    }

    log(LOG_DEBUG, "Registered socket '%i' on epoll with events %u", fd,
        events);
    return true;
}

bool WebServer::unregister_epoll_events(int fd) {
    WebServer* server = get_instance();
    if (!server) {
        log(LOG_FATAL,
            "WebServer instance is NULL, cannot unregister epoll events");
        return false;
    }

    if (epoll_ctl(server->epoll_fd_, EPOLL_CTL_DEL, fd, NULL) < 0) {
        log(LOG_ERROR, "Failed to unregister socket '%i' on epoll", fd);
        return false;
    }

    log(LOG_DEBUG, "Unregistered socket '%i' on epoll", fd);
    return true;
}

bool WebServer::update_epoll_events(int fd, uint32_t events) {
    WebServer* server = get_instance();
    if (!server) {
        log(LOG_FATAL,
            "WebServer instance is NULL, cannot update epoll events");
        return false;
    }

    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.fd = fd;

    if (epoll_ctl(server->epoll_fd_, EPOLL_CTL_MOD, fd, &event) < 0) {
        log(LOG_ERROR, "Failed to up epoll events for socket '%i'", fd);
        return false;
    }

    log(LOG_DEBUG, "Updated epoll events for socket '%i' to %u", fd, events);
    return true;
}

void WebServer::register_active_pipe(int pipe_fd, Connection* conn) {
    WebServer* server = get_instance();
    if (!server) {
        log(LOG_FATAL,
            "WebServer instance is NULL, cannot register active pipe");
        return;
    }

    server->get_conn_manager()->register_pipe(pipe_fd, conn);
}

void WebServer::unregister_active_pipe(int pipe_fd) {
    WebServer* server = get_instance();
    if (!server) {
        log(LOG_FATAL,
            "WebServer instance is NULL, cannot unregister active pipe");
        return;
    }

    server->get_conn_manager()->unregister_pipe(pipe_fd);
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
    // CHECK the extension allowed for CGI - Carol
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

// TODO -----------------------------------------------------------------------

// Dentro de handle_client_socket_event
codes::ParseStatus WebServer::process_request(Connection* conn) {
    log(LOG_DEBUG, "Processing request for connection: %i", conn->client_fd_);
    // Phase 1: Estabilish context
    // a. Match host header with virtual server
    // b. Match best location block within virtual server
    // Phase 2: Validate request
    // c. Validate version (and host header for HTTP/1.1)
    // d. Validate request method is valid and allowed
    // e. Validate content length or transfer encoding: chunked
    // Phase 3: Choose handler and validate permissions
    // f. Choose handler based on request method and location (choose_handler
    // function) g. Validate permissions
    // (active_handler_->validate_permissions(conn)) 
	// Phase 4: Prepare for body handling and execution 
	// h. Determine if body is needed and what kind

	// after that: parse_body (if needed) and setup_next_event_state

    // REFACTOR AND DELETE
    // Host header required for HTTP/1.1
    HttpRequest* request = conn->request_data_;
    if (request->version_ == "HTTP/1.1" &&
        request->get_header("host").empty()) {
        // Translates to response status 400
        log(LOG_ERROR,
            "Missing Host header in HTTP/1.1 request for connection: %i",
            conn->client_fd_);
        return codes::PARSE_MISSING_HOST_HEADER;
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
            return codes::PARSE_MISSING_CONTENT_LENGTH;
        }

        if (has_content_length && has_transfer_encoding) {
            // Translates to response status 400
            log(LOG_ERROR,
                "POST/PUT with both Content-Length and Transfer-Encoding");
            return codes::PARSE_INVALID_CONTENT_LENGTH;
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
                return codes::PARSE_INVALID_CONTENT_LENGTH;
            }

            if (body_size > conn->virtual_server_->client_max_body_size_) {
                log(LOG_ERROR, "Content-Length exceeds maximum size: %zu",
                    body_size);
                // Translates to response status 413
                return codes::PARSE_CONTENT_TOO_LARGE;
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
                return codes::PARSE_UNKNOWN_ENCODING;
            }
        }
    }

    log(LOG_DEBUG, "Headers validated successfully for connection: %i",
        conn->client_fd_);
    return codes::PARSE_SUCCESS;
}  // REFACTOR AND DELETE

AHandler* WebServer::choose_handler(Connection* conn) {
    log(LOG_DEBUG,
        "choose_handler: Finding handler for client_fd %d, method %s, path %s",
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
        conn->conn_state_ = codes::EXECUTING_CGI;
        return cgi_handler_;
    } else if (request_method == "POST") {
        // FileUploadHandler for file uploads
        log(LOG_DEBUG,
            "choose_handler: Using FileUploadHandler for client_fd %d, path %s",
            conn->client_fd_, matching_location->path_.c_str());
        conn->conn_state_ = codes::GENERATING_RESPONSE;
        return file_upload_handler_;
    } else if (request_method == "DELETE") {
        // DeleteHandler for dlete requests
        log(LOG_DEBUG,
            "choose_handler: Using DeleteHandler for client_fd %d, path %s",
            conn->client_fd_, matching_location->path_.c_str());
        conn->conn_state_ = codes::GENERATING_RESPONSE;
        return file_delete_handler_;
    } else {
        // Default to StaticFileHandler for regular files
        log(LOG_DEBUG,
            "choose_handler: Using StaticFileHandler for client_fd %d, path %s",
            conn->client_fd_, matching_location->path_.c_str());
        conn->conn_state_ = codes::GENERATING_RESPONSE;
        return static_file_handler_;
    }
}

codes::ConnectionState WebServer::determine_body_handling_state(
    Connection* conn) {
    HttpRequest* request = conn->request_data_;
    // Check for request body
    if (request->method_ == "POST" || request->method_ == "PUT") {
        // Check for Transfer-Encoding header
        std::string transfer_encoding =
            request->get_header("transfer-encoding");
        if (!transfer_encoding.empty() &&
            transfer_encoding.find("chunked") != std::string::npos) {
            return codes::PARSING_CHUNKED_BODY;
        }

        // Check for Content-Length header
        std::string content_length = request->get_header("content-length");
        if (!content_length.empty()) {
            char* end_ptr;
            size_t body_size =
                std::strtoul(content_length.c_str(), &end_ptr, 10);

            if (body_size > 0) {
                return codes::PARSING_BODY;
            }
        }
    }

    // No body needed or zero-length body
    return codes::GENERATING_RESPONSE;
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
            "Location details: path=%s, root=%s, autoindex=%d, cgi_enabled=%d, "
            "allowed_methods=%s, index=%s, redirect=%s",
            best_match->path_.c_str(), best_match->root_.c_str(),
            best_match->autoindex_, best_match->cgi_enabled_,
            allowed_methods_str.c_str(), best_match->index_.c_str(),
            best_match->redirect_.c_str());
    }

    return best_match;
}

// ------------- TODO: fix functions on the corner of shame below -----------

bool WebServer::validate_request_location(Connection* conn) {
    const Location* matching_location = conn->location_match_;
    if (!matching_location) {
        log(LOG_ERROR, "No matching location found for request path: %s",
            conn->request_data_->path_.c_str());
        ErrorHandler::generate_error_response(conn, codes::NOT_FOUND);
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
                "Connection '%i', Host '%s': Method not allowed: %s, Allowed "
                "methods: %s",
                conn->client_fd_, conn->virtual_server_->host_name_.c_str(),
                request_method.c_str(), allowed_methods_str.c_str());

            // Apply 405 error directly to the response
            ErrorHandler::generate_error_response(conn,
                                                  codes::METHOD_NOT_ALLOWED);
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
        "Connection '%i', Host '%s': Request method '%s' is allowed for path "
        "'%s'",
        conn->client_fd_, conn->virtual_server_->host_name_.c_str(),
        request_method.c_str(), matching_location->path_.c_str());
    return true;
}

// TODO: improve this ugly ass function
void WebServer::match_host_header(Connection* conn) {
    if (!conn || !conn->request_data_ || !conn->default_virtual_server_) {
        log(LOG_FATAL, "match_host_header: Invalid connection or data.");
        // conn->virtual_server_ should already be default_virtual_server_ or
        // NULL if creation failed
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

    // The connection's default_virtual_server_ tells us the port and listen IP
    // this connection is associated with.
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
                        break;  // Found specific server_name match on specific
                                // IP
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
            "Matched Host header '%s' to virtual server with primary name '%s' "
            "on %s:%d",
            request_host_header_val.c_str(),
            matched_vs->server_names_.empty()
                ? matched_vs->host_name_.c_str()
                : matched_vs->server_names_[0].c_str(),
            matched_vs->host_.c_str(), matched_vs->port_);
        conn->virtual_server_ = matched_vs;
    } else {
        // No specific server_name match found, conn->virtual_server_ remains as
        // conn->default_virtual_server_.
        log(LOG_DEBUG,
            "No specific virtual server for Host header '%s'. Using default "
            "for listener %s:%d (primary name '%s').",
            request_host_header_val.c_str(),
            conn->virtual_server_->host_.c_str(), conn->virtual_server_->port_,
            conn->virtual_server_->server_names_.empty()
                ? conn->virtual_server_->host_name_.c_str()
                : conn->virtual_server_->server_names_[0].c_str());
    }
}
