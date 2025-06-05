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
      file_upload_handler_(NULL) {
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
            if (VirtualServer::parse_server_block(file, virtual_server)) {
                std::string error_msg;
                if (!virtual_server.is_valid(error_msg)) {
                    log(LOG_ERROR,
                        "Error: Invalid virtual server configuration: %s",
                        error_msg.c_str());
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

            if (log(LOG_TRACE, "Parsed virtual server configuration:") > 0) {
                print_virtual_server(virtual_server);
            }

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
    } else if ((events & EPOLLIN) && conn->is_readable()) {
        handle_read(conn);
    } else if (((events & EPOLLOUT) && conn->is_writable()) || conn->is_cgi()) {
        handle_write(conn);
    } else {
        log(LOG_FATAL,
            "handle_connection_event: Unhandled event %u for client_fd %d",
            events, client_fd);
        handle_error(conn);
    }
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

void WebServer::handle_read(Connection* conn) {
    log(LOG_DEBUG, "handle_read: Starting for client_fd %d", conn->client_fd_);

    // Read data from the socket
    if (!request_parser_->read_from_socket(conn)) {
        log(LOG_ERROR,
            "handle_read: Failed to read from socket for client_fd %d",
            conn->client_fd_);
        handle_error(conn);
        return;
    }

    // Try to parse the buffer into a full request
    conn->parse_status_ = request_parser_->parse(conn);

    // If we have completed parsing headers, we need to match the host header
    if (conn->parse_status_ == codes::PARSE_HEADERS_COMPLETE) {
        log(LOG_DEBUG,
            "handle_read: Headers complete, matching host for client_fd %d",
            conn->client_fd_);
        match_host_header(conn);
        // Re-parse the request with the matched virtual server
        conn->parse_status_ = request_parser_->parse(conn);
    }

    // If request parsing is incomplete, return and wait for more data
    if (conn->parse_status_ == codes::PARSE_INCOMPLETE) {
        log(LOG_DEBUG,
            "handle_read: Parsing incomplete for client_fd %d, waiting for "
            "more data",
            conn->client_fd_);
        return;
    }

    if (log(LOG_TRACE, "Printing request for debugging:") > 0) {
        print_request(conn);
    }

    // Match path from uri to best location
    conn->location_match_ = find_matching_location(conn->virtual_server_,
                                                   conn->request_data_->path_);

    // Change epoll interest event to EPOLLOUT for writing
    update_epoll_events(conn->client_fd_, EPOLLOUT);
    conn->conn_state_ = codes::CONN_PROCESSING;
}

void WebServer::handle_write(Connection* conn) {
    if (!conn) {
        log(LOG_FATAL, "handle_write: Connection pointer is NULL");
        return;
    }

    if (!conn->request_data_) {
        log(LOG_FATAL, "handle_write: Request data is NULL for client_fd %d",
            conn->client_fd_);
        handle_error(conn);
        return;
    }

     log(LOG_DEBUG, "handle_write: Pre-access check. client_fd %d. request_data_ pointer: %p", conn->client_fd_, static_cast<void*>(conn->request_data_));

    const char* method_cstr = NULL;
    const char* path_cstr = NULL;

    try {
        // Verifica method_
        if (conn->request_data_->method_.empty()) {
            log(LOG_DEBUG, "handle_write: conn->request_data_->method_ is empty for client_fd %d.", conn->client_fd_);
        }
        log(LOG_DEBUG, "handle_write: method_.length() = %zu for client_fd %d", conn->request_data_->method_.length(), conn->client_fd_);
        method_cstr = conn->request_data_->method_.c_str();
        log(LOG_DEBUG, "handle_write: Successfully obtained method_.c_str() for client_fd %d. Pointer: %p", conn->client_fd_, static_cast<const void*>(method_cstr));

        // Verifica path_
        if (conn->request_data_->path_.empty()) {
            log(LOG_DEBUG, "handle_write: conn->request_data_->path_ is empty for client_fd %d.", conn->client_fd_);
        }
        log(LOG_DEBUG, "handle_write: path_.length() = %zu for client_fd %d", conn->request_data_->path_.length(), conn->client_fd_);
        path_cstr = conn->request_data_->path_.c_str();
        log(LOG_DEBUG, "handle_write: Successfully obtained path_.c_str() for client_fd %d. Pointer: %p", conn->client_fd_, static_cast<const void*>(path_cstr));

    } catch (const std::exception& e) {
        log(LOG_ERROR, "handle_write: Exception while accessing method/path string members for client_fd %d: %s. request_data_ pointer: %p",
            conn->client_fd_, e.what(), static_cast<void*>(conn->request_data_));
        // Se houver uma exceção aqui, é um sinal de corrupção de memória severa.
        // Gerar um erro interno e fechar a conexão é o mais seguro.
        ErrorHandler::generate_error_response(conn, codes::INTERNAL_SERVER_ERROR);
        // Certifique-se de que o estado da conexão e os eventos epoll sejam atualizados para escrita/fechamento.
        if (conn->conn_state_ != codes::CONN_CGI_EXEC) { // CGI tem seu próprio fluxo de escrita
             conn->conn_state_ = codes::CONN_WRITING;
        }
        update_epoll_events(conn->client_fd_, EPOLLOUT | EPOLLRDHUP); // Tenta enviar o erro e detecta fechamento
        return; // Não prosseguir se os dados da requisição estiverem corrompidos
    }

    if (method_cstr == NULL || path_cstr == NULL) {
        log(LOG_ERROR, "handle_write: method_cstr or path_cstr is NULL after try-catch for client_fd %d. This indicates a problem. Method ptr: %p, Path ptr: %p",
            conn->client_fd_, static_cast<const void*>(method_cstr), static_cast<const void*>(path_cstr));
        ErrorHandler::generate_error_response(conn, codes::INTERNAL_SERVER_ERROR);
        if (conn->conn_state_ != codes::CONN_CGI_EXEC) {
             conn->conn_state_ = codes::CONN_WRITING;
        }
        update_epoll_events(conn->client_fd_, EPOLLOUT | EPOLLRDHUP);
        return;
    }
    // --- Fim das verificações de depuração adicionadas ---

    log(LOG_DEBUG,
        "handle_write: Processing request method=%s, path=%s for client_fd %d",
        method_cstr, // Usa os ponteiros obtidos com segurança
        path_cstr,   // Usa os ponteiros obtidos com segurança
        conn->client_fd_);

    log(LOG_DEBUG, "handle_write: Initial conn->parse_status_ = %d for client_fd %d", conn->parse_status_, conn->client_fd_);

    // log(LOG_DEBUG,
    //     "handle_write: Processing request method=%s, path=%s for client_fd %d",
    //     conn->request_data_->method_.c_str(),
    //     conn->request_data_->path_.c_str(), conn->client_fd_);

    if (conn->parse_status_ != codes::PARSE_SUCCESS) {
        log(LOG_WARNING, "handle_write: Invalid request from client_fd %d",
            conn->client_fd_);
        ErrorHandler::generate_error_response(conn);
    }
    
    log(LOG_DEBUG, "handle_write: conn->conn_state_ = %d before handler logic for client_fd %d", conn->conn_state_, conn->client_fd_);

    if (conn->conn_state_ == codes::CONN_PROCESSING ||
        conn->conn_state_ == codes::CONN_CGI_EXEC) {
        bool can_execute_handler = true;
        log(LOG_DEBUG, "handle_write: [Checkpoint 1] Inside handler logic block for client_fd %d.", conn->client_fd_); // NOVO LOG

        // Route the request to the appropriate handler
        if (!conn->active_handler_) {
            log(LOG_DEBUG, "handle_write: conn->active_handler_ IS NULL. Validating request location for client_fd %d.", conn->client_fd_);
            if (!validate_request_location(conn)) {
                // ErrorHandler::generate_error_response was already called
                can_execute_handler = false;
            } else {
                // Choose handler always return a handler
                conn->active_handler_ = choose_handler(conn);
            }
        }
        // Call the handler to process the request and generate a response
        if (conn->active_handler_ && can_execute_handler) {
            conn->active_handler_->handle(conn);
        }
    }

    // // TEMP - Call StaticFileHandler to test
    //  conn->active_handler_ = static_file_handler_;
    //  log(LOG_DEBUG, "handle_write: Using static_file_handler for client_fd
    //  %d", conn->client_fd_); conn->active_handler_->handle(conn);
    //  // Print the HTTP response for debugging
    //  std::cout << "\n==== HTTP RESPONSE ====\n";
    //  std::cout << "Status: " << conn->response_data_->status_code_ << " "
    //          << conn->response_data_->status_message_ << std::endl;
    // std::cout << "Headers:" << std::endl;
    // for (std::map<std::string, std::string>::const_iterator it =
    //      conn->response_data_->headers_.begin();
    //      it != conn->response_data_->headers_.end(); ++it) {
    //     std::cout << "  " << it->first << ": " << it->second << std::endl;
    // }
    // std::cout << "Body size: " << conn->response_data_->body_.size() <<
    //              "bytes" << std::endl;
    // std::cout << "====================================" << std::endl;

    //// // TEMP - Call FileUploadHandler to test
    // conn->active_handler_ = file_upload_handler_;
    // log(LOG_DEBUG, "handle_write: Using file_upload_handler for client_fd
    // %d",
    //     conn->client_fd_);
    // conn->active_handler_->handle(conn);
    //// Print the HTTP response for debugging
    // std::cout << "\n==== HTTP RESPONSE ====\n";
    // std::cout << "Status: " << conn->response_data_->status_code_ << " "
    //           << conn->response_data_->status_message_ << std::endl;
    // std::cout << "Headers:" << std::endl;
    // for (std::map<std::string, std::string>::const_iterator it =
    //          conn->response_data_->headers_.begin();
    //      it != conn->response_data_->headers_.end(); ++it) {
    //     std::cout << "  " << it->first << ": " << it->second << std::endl;
    // }
    // std::cout << "Body size: " << conn->response_data_->body_.size() << "
    // bytes"
    //           << std::endl;
    // std::cout << "====================================" << std::endl;

    // TEMP - For now, create mock response
    // build_mock_response(conn);
    // TEMP

    if (conn->conn_state_ == codes::CONN_WRITING) {
        // Write the response to the client
        codes::WriteStatus status = response_writer_->write_response(conn);

        // TODO - Remove switch state, call logs and handle_error before
        // returning the status. Leave condition if(status ==
        // codes::WRITING_INCOMPLETE) { return; } because if it is incomplete,
        // we should return and wait for the next call, and if it is an writing
        // error, the connection should be closed inse write_response, never
        // reaching this point. Only WRITING_SUCCESS will continue the flow.
        switch (status) {
            case codes::WRITING_INCOMPLETE:
                log(LOG_DEBUG,
                    "handle_write: Response writing incomplete for client_fd "
                    "%d, "
                    "will resume later",
                    conn->client_fd_);
                return;
            case codes::WRITING_ERROR:
                log(LOG_ERROR,
                    "handle_write: Error writing response to client_fd %d",
                    conn->client_fd_);
                handle_error(conn);
                return;
            case codes::WRITING_SUCCESS:
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

        // TODO - Change condition to function
        // is_unrecoverable_error(status_code) { return status_code == 400 ||
        // status_code == 413 || status_code >= 500; ... }
        if (status_code == 400 || status_code == 413 || status_code >= 500) {
            // Close connections for client and server errors
            log(LOG_INFO,
                "handle_write: Closing connection for error status %d on "
                "client_fd "
                "%d",
                status_code, conn->client_fd_);
            conn->request_data_->set_header("Connection", "close");
        }

        if (conn->is_keep_alive()) {
            log(LOG_DEBUG,
                "handle_write: Keep-alive enabled, resetting connection for "
                "client_fd %d",
                conn->client_fd_);
            conn->reset_for_keep_alive();
            update_epoll_events(conn->client_fd_, EPOLLIN);
        } else {
            log(LOG_DEBUG,
                "handle_write: Keep-alive not enabled, closing connection for "
                "client_fd %d",
                conn->client_fd_);
            close_client_connection(conn);
        }
        // handle_keep_alive end
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
        log(LOG_ERROR, "Failed to bind to %s:%i", host.c_str(), port);
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
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.fd = fd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) < 0) {
        log(LOG_ERROR, "Failed to register socket '%i' on epoll", fd);
        return false;
    }

    return true;
}

bool WebServer::update_epoll_events(int fd, uint32_t events) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.fd = fd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event) < 0) {
        log(LOG_ERROR, "Failed to up epoll events for socket '%i'", fd);
        return false;
    }

    log(LOG_DEBUG, "Updated epoll events for socket '%i' to %u", fd, events);
    return true;
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

    struct sigaction sa_chld;
    sa_chld.sa_handler = signal_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags =
        SA_RESTART | SA_NOCLDSTOP;  // Avoids stopping on child exit
    if (sigaction(SIGCHLD, &sa_chld, NULL) < 0) {
        log(LOG_ERROR, "Failed to set up SIGCHLD handler");
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
    } else if (signal == SIGCHLD) {
        // Reap zombie processes
        int saved_errno = errno;  // Preserve errno
        pid_t pid;
        while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
            // Child reaped
            log(LOG_DEBUG, "SIGCHLD handler: Reaped child process '%d'.", pid);
        }
        errno = saved_errno;  // Restore errno
    }
}

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

AHandler* WebServer::choose_handler(Connection* conn) {
    log(LOG_DEBUG,
        "choose_handler: Finding handler for client_fd %d, method %s, path %s",
        conn->client_fd_, conn->request_data_->method_.c_str(),
        conn->request_data_->path_.c_str());

    const Location* matching_location = conn->location_match_;
    const std::string& request_method = conn->request_data_->method_;

    // TODO - Change CGI condition to cgi_enabled + executable file + valid
    // script extension?
    // Return appropriate handler based on location config
    if (matching_location->cgi_enabled_) {
        // CGI handler for CGI-enabled locations
        log(LOG_DEBUG,
            "choose_handler: Using CgiHandler for client_fd %d, path %s",
            conn->client_fd_, matching_location->path_.c_str());
        conn->conn_state_ = codes::CONN_CGI_EXEC;
        return cgi_handler_;
    } else if (request_method == "POST" || request_method == "DELETE") {
        // FileUploadHandler for file uploads
        log(LOG_DEBUG,
            "choose_handler: Using FileUploadHandler for client_fd %d, path %s",
            conn->client_fd_, matching_location->path_.c_str());
        conn->conn_state_ = codes::CONN_PROCESSING;
        return file_upload_handler_;
    } else {
        // Default to StaticFileHandler for regular files
        log(LOG_DEBUG,
            "choose_handler: Using StaticFileHandler for client_fd %d, path %s",
            conn->client_fd_, matching_location->path_.c_str());
        conn->conn_state_ = codes::CONN_PROCESSING;
        return static_file_handler_;
    }
}

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
