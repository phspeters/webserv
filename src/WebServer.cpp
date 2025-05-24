#include "webserv.hpp"

WebServer* WebServer::instance_ = NULL;

WebServer::WebServer()
    : epoll_fd_(-1),
      ready_(false),
      conn_manager_(NULL),
      request_parser_(NULL),
      response_writer_(NULL),
      static_file_handler_(NULL),
      // cgi_handler_(NULL),
      file_upload_handler_(NULL) {
    instance_ = this;
}

WebServer::~WebServer() {
    // Clean up owned components
    delete conn_manager_;
    delete request_parser_;
    delete response_writer_;
    delete static_file_handler_;
    // delete cgi_handler_;
    delete file_upload_handler_;

    // Close listener sockets if they are open
    for (std::vector<int>::iterator it = listener_fds_.begin();
         it != listener_fds_.end(); ++it) {
        if (*it != -1) {
            close(*it);
        }
    }

    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
    }
}

bool WebServer::init() {
    try {
        // Initialize components
        conn_manager_ = new ConnectionManager();
        request_parser_ = new RequestParser();
        response_writer_ = new ResponseWriter();

        // Initialize handlers
        static_file_handler_ = new StaticFileHandler();
        // cgi_handler_ = new CgiHandler();
        file_upload_handler_ = new FileUploadHandler();
    } catch (const std::bad_alloc& e) {
        log(LOG_ERROR, "Memory allocation failed: %s", e.what());
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
        // Handle error
        log(LOG_ERROR, "Failed to create epoll instance");
        return false;
    }

    // Set up the listener sockets
    if (!setup_listener_sockets()) {
        // Handle error
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
                // Add to main vector
                virtual_servers_.push_back(virtual_server);

                // Store pointer to the newly added server (vector might
                // reallocate)
                VirtualServer* server_ptr = &virtual_servers_.back();

                // Map hostnames for Host header matching
                for (std::vector<std::string>::const_iterator it =
                         server_ptr->server_names_.begin();
                     it != server_ptr->server_names_.end(); ++it) {
                    hostname_to_virtual_server_[*it] = server_ptr;
                }

                // Group by port first, then by host
                int port = server_ptr->port_;
                std::string host = server_ptr->host_;
                port_to_hosts_[port][host].push_back(server_ptr);
            }

            // TEMP- Print the parsed configuration
            print_virtual_server(virtual_server);
            // TEMP

        } else {
            log(LOG_ERROR, "Error parsing server block");
            return false;  // Parsing error
        }
    }

    // Close the file
    file.close();

    return true;
}

void WebServer::run() {
    ready_ = true;

    // Start the event loop
    log(LOG_INFO, "WebServer is ready and waiting for connections");
    event_loop();
}

void WebServer::shutdown() { ready_ = false; }

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
            // Handle error or signal interruption
            if (errno == EINTR) {
                continue;  // Interrupted by signal, continue loop
            }
            log(LOG_ERROR, "epoll_wait error: %s", strerror(errno));
            break;
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
                // Handle client socket event
                log(LOG_INFO, "Client event on socket '%i'", fd);
                handle_client_event(fd, event_flags);
            }
        }
    }
}

void WebServer::accept_new_connection(int listener_fd) {
    // Find the default virtual server for this listener
    VirtualServer* default_server = NULL;
    if (listener_to_default_server_.find(listener_fd) !=
        listener_to_default_server_.end()) {
        default_server = listener_to_default_server_[listener_fd];
    } else {
        log(LOG_ERROR, "No default server found for listener socket '%i'",
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

void WebServer::handle_client_event(int client_fd, uint32_t events) {
    Connection* conn = conn_manager_->get_connection(client_fd);
    if (!conn) {
        // Handle error
        log(LOG_ERROR, "Connection not found for client socket '%i'",
            client_fd);
        return;
    }

    if (events & EPOLLIN) {
        handle_read(conn);
    } else if (events & EPOLLOUT) {
        handle_write(conn);
    } else if (events & (EPOLLERR | EPOLLHUP)) {
        handle_error(conn);
    }
}

void WebServer::handle_read(Connection* conn) {
    // Read data from the socket
    if (!request_parser_->read_from_socket(conn)) {
        handle_error(conn);
        return;
    }

    // Try to parse the buffer into a full request
    conn->parse_status_ = request_parser_->parse(conn);

    // If we have completed parsing headers, we need to match the host header
    if (conn->parse_status_ == codes::PARSE_HEADERS_COMPLETE) {
        match_host_header(conn);
        // Re-parse the request with the matched virtual server
        conn->parse_status_ = request_parser_->parse(conn);
    }

    // If request parsing is incomplete, return and wait for more data
    if (conn->parse_status_ == codes::PARSE_INCOMPLETE) {
        return;
    }

    // TEMP - Print request to console
    print_request(conn);
    // TEMP

    // Match path from uri to best location
    conn->location_match_ = find_matching_location(conn->virtual_server_,
                                                   conn->request_data_->path_);

    // Change epoll interest event to EPOLLOUT for writing
    update_epoll_events(conn->client_fd_, EPOLLOUT);
    conn->conn_state_ = codes::CONN_WRITING;
}

void WebServer::handle_write(Connection* conn) {
    //// TODO - Check if request is valid
    // if (!validate_request(conn)) {
    //     // TODO - Write function to generate response based on request error
    //     conn->response_data_ = generate_error_response(conn);
    // } else {
    //     // TODO - Review choose_handler function
    //     // Route the request to the appropriate handler
    //     if (!conn->active_handler_) {
    //         conn->active_handler_ = choose_handler(conn);
    //     }
    //     // Call the handler to process the request and generate a response
    //     conn->response_data_ = conn->active_handler_->handle(conn);
    // }

    // // TEMP - Call StaticFileHandler to test
    //  conn->active_handler_ = static_file_handler_;
    //  conn->active_handler_->handle(conn);
    //  // Print the HTTP response for debugging
    //  std::cout << "\n==== HTTP RESPONSE ====\n";
    //  std::cout << "Status: " << conn->response_data_->status_code_ << " "
    //          << conn->response_data_->status_message_ << std::endl;
    //  std::cout << "Headers:" << std::endl;
    //  for (std::map<std::string, std::string>::const_iterator it =
    //  conn->response_data_->headers_.begin();
    //      it != conn->response_data_->headers_.end(); ++it) {
    //      std::cout << "  " << it->first << ": " << it->second << std::endl;
    //  }
    //  std::cout << "Body size: " << conn->response_data_->body_.size() <<
    //  "bytes" << std::endl; std::cout <<
    //  "====================================" << std::endl;

    // TEMP - For now, create mock response
    build_mock_response(conn);
    // TEMP

    // Write the response to the client
    codes::WriterState status = response_writer_->write_response(conn);

    switch (status) {
        case codes::WRITING_INCOMPLETE:
            return;
        case codes::WRITING_ERROR:
            handle_error(conn);
            return;
        case codes::WRITING_COMPLETE:
            break;
    }

    // Check for error status codes that should close the connection
    int status_code = conn->response_data_->status_code_;
    if (status_code == 400 || status_code == 413 || status_code >= 500) {
        // Close connections for client and server errors
        conn->request_data_->set_header("Connection", "close");
    }

    if (conn->is_keep_alive()) {
        conn->reset_for_keep_alive();
        update_epoll_events(conn->client_fd_, EPOLLIN);
    } else {
        close_client_connection(conn);
    }
}

void WebServer::handle_error(Connection* conn) {
    // Handle error
    close_client_connection(conn);
}

void WebServer::close_client_connection(Connection* conn) {
    if (!conn) {
        log(LOG_ERROR, "Connection is invalid, cannot close.");
        return;
    }

    // First unregister from epoll (must happen before socket closure)
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, conn->client_fd_, NULL) < 0) {
        // Handle error
        log(LOG_ERROR, "Failed to unregister socket %i from epoll",
            conn->client_fd_);
        return;
    }

    // Then let connection manager handle the rest
    conn_manager_->close_connection(conn);
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
        log(LOG_ERROR, "Failed to remove listener '%i' from epoll: %s", fd, strerror(errno));
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
        // Handle error
        log(LOG_ERROR, "Failed to get flags for socket '%i'", fd);
        return false;
    }

    flags |= O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) == -1) {
        // Handle error
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
        // Handle error
        log(LOG_ERROR, "Failed to register socket '%i'", fd);
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
        // Handle error
        log(LOG_ERROR, "Failed to up epoll events for socket '%i'", fd);
        return false;
    }

    return true;
}

bool WebServer::setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) < 0) {
        // Handle error
        log(LOG_ERROR, "Failed to set up SIGINT handler");
        return false;
    }

    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        // Handle error
        log(LOG_ERROR, "Failed to set up SIGTERM handler");
        return false;
    }

    return true;
}

void WebServer::signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        get_instance()->shutdown();
        log(LOG_INFO, "Received shutdown signal. Exiting...");
    }
}

const Location* WebServer::find_matching_location(
    const VirtualServer* virtual_server, const std::string& uri) const {
    std::vector<Location> locations_ = virtual_server->locations_;
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

    return best_match;
}

bool WebServer::choose_handler(Connection* conn) {
    HttpRequest* req = conn->request_data_;

    // Get the path from the request (already without query part)
    const std::string& request_path = req->path_;
    const std::string& request_method = req->method_;

    // Use centralized location matching from config
    const Location* matching_location = req->location_match_;

    // Check if a matching location was found
    if (!matching_location) {
        std::cout << "\n==== ROUTER ERROR ====\n";
        std::cout << "No matching location for path: " << request_path
                  << std::endl;
        std::cout << "======================\n" << std::endl;

        ErrorHandler::not_found(conn->response_data_, *(conn->virtual_server_));
        // Return default handler or handle error case
        conn->active_handler_ = static_file_handler_;
        return false;  // Usually you'd return an error handler
                       // instead
    }

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
            std::cout << "\n==== ROUTER ERROR ====\n";
            std::cout << "Method not allowed: " << request_method << std::endl;
            std::cout << "Allowed methods: " << allowed_methods_str
                      << std::endl;
            std::cout << "======================\n" << std::endl;

            // Apply 405 error directly to the response
            ErrorHandler::method_not_allowed(conn->response_data_,
                                             *(conn->virtual_server_));

            // Add the Allow header
            conn->response_data_->headers_["Allow"] = allowed_methods_str;

            conn->active_handler_ = static_file_handler_;
            return false;  // Return static handler but response is already set
        }
    }

    // Print router info when a location is matched
    std::cout << "\n==== ROUTER INFO ====\n";
    std::cout << "Request path: " << request_path << std::endl;
    std::cout << "Matching location: " << matching_location->path_ << std::endl;
    std::cout << "Autoindex: "
              << (matching_location->autoindex_ ? "true" : "false")
              << std::endl;
    std::cout << "CGI enabled: "
              << (matching_location->cgi_enabled_ ? "true" : "false")
              << std::endl;
    std::cout << "Root: " << matching_location->root_ << std::endl;
    std::cout << "======================\n" << std::endl;

    // Return appropriate handler based on location config
    if (matching_location->cgi_enabled_) {
        // CGI handler for CGI-enabled locations
        conn->active_handler_ = cgi_handler_;
        return true;
    } else if (request_method == "POST" || request_method == "DELETE") {
        // FileUploadHandler for file uploads
        conn->active_handler_ = file_upload_handler_;
        return true;
    } else {
        // Default to StaticFileHandler for regular files
        conn->active_handler_ = static_file_handler_;
        return true;
    }
}

void WebServer::match_host_header(Connection* conn) {
    // Get Host header value
    std::string host = conn->request_data_->get_header("Host");
    if (host != "") {
        // Strip port number if present
        size_t colon_pos = host.find(':');
        if (colon_pos != std::string::npos) {
            host = host.substr(0, colon_pos);
        }

        // Look for matching virtual server
        if (hostname_to_virtual_server_.find(host) !=
            hostname_to_virtual_server_.end()) {
            conn->virtual_server_ = hostname_to_virtual_server_[host];
        }
    }
    // If no match is found, conn->virtual_server_ remains as the default
}
