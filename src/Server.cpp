#include "webserv.hpp"

Server::Server(const ServerConfig& config, ServerManager* manager)
    : server_manager_(manager),
      config_(config),
      listener_fd_(-1),
      initialized_(false),
      conn_manager_(NULL),
      request_parser_(NULL),
      router_(NULL),
      response_writer_(NULL),
      static_file_handler_(NULL),
      //  cgi_handler_(NULL),
      file_upload_handler_(NULL) {}

Server::~Server() {
    // Clean up owned components
    delete conn_manager_;
    delete request_parser_;
    delete router_;
    delete response_writer_;
    delete static_file_handler_;
    // delete cgi_handler_;
    delete file_upload_handler_;

    // Close listener socket if it's open
    if (listener_fd_ != -1) {
        // Unregister from epoll first
        server_manager_->unregister_fd(listener_fd_);
        // Then close the socket
        close(listener_fd_);
        listener_fd_ = -1;
    }
}

bool Server::init() {
    // Initialize components
    conn_manager_ = new ConnectionManager(config_);
    request_parser_ = new RequestParser(config_);
    response_writer_ = new ResponseWriter(config_);

    // Initialize handlers
    static_file_handler_ = new StaticFileHandler(config_);
    // cgi_handler_ = new CgiHandler(config_);
    file_upload_handler_ = new FileUploadHandler(config_);

    // Initialize the router with the handlers
    router_ = new Router(config_, static_file_handler_, cgi_handler_,
                         file_upload_handler_);

    // Set up the listener socket and epoll instance
    if (!setup_listener_socket()) {
        // Handle error
        return false;
    }

    initialized_ = true;
    std::cout << "Server initialized successfully: " << config_.server_names_[0]
              << " on port " << config_.port_ << std::endl;

    return true;
}

void Server::accept_new_connection(std::map<int, Server*>& fd_map) {
    // Accept a new connection and set it to non-blocking mode
    int client_fd = accept4(listener_fd_, NULL, NULL, SOCK_NONBLOCK);
    if (client_fd < 0) {
        // Handle error
        std::cerr << "Failed to accept new connection for server "
                  << config_.server_names_[0] << "on port " << config_.port_
                  << std::endl;
        return;
    }

    // Register with epoll for read events
    if (!register_epoll_events(client_fd)) {
        // Handle error
        close(client_fd);
        return;
    }

    // Create connection
    Connection* conn = conn_manager_->create_connection(client_fd);
    if (!conn) {
        // Handle error
        close(client_fd);
    }

    // Add to ServerManager's map
    fd_map[client_fd] = this;
}

void Server::close_client_connection(Connection* conn) {
    if (!conn) {
        std::cerr << "Connection is invalid, cannot close." << std::endl;
        return;
    }

    int client_fd = conn->client_fd_;

    // First unregister from epoll (must happen before socket closure)
    server_manager_->unregister_fd(client_fd);

    // Then let connection manager handle the rest
    conn_manager_->close_connection(conn);
}

void Server::handle_client_event(int client_fd, uint32_t events) {
    Connection* conn = conn_manager_->get_connection(client_fd);
    if (!conn) {
        // Handle error
        std::cerr << "Connection not found for fd: " << client_fd
                  << " on server " << config_.server_names_[0] << " on port "
                  << config_.port_ << std::endl;
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

void Server::handle_read(Connection* conn) {
    // Read data from the socket
    if (!request_parser_->read_from_socket(conn)) {
        handle_error(conn);
        return;
    }

    // Try to parse the buffer into a full request
    RequestParser::ParseResult status = request_parser_->parse(conn);

    // If request parsing is incomplete, return and wait for more data
    if (status == RequestParser::PARSE_INCOMPLETE) {
        return;
    }

    // TEMP - Print request to console
    print_request(conn);
    // TEMP

    // Change epoll interest event to EPOLLOUT for writing
    update_epoll_events(conn->client_fd_, EPOLLOUT);
}

void Server::handle_write(Connection* conn) {
    // Route the request to the appropriate handler
    AHandler* handler = router_->route(conn->request_data_);

    // Call the handler to process the request and generate a response
    handler->handle(conn);

    // TEMP print the http response managed by the handler
    std::cout << "\n==== HTTP RESPONSE ====\n";
    std::cout << "Status: " << conn->response_data_->status_code_ << " " 
              << conn->response_data_->status_message_ << std::endl;
    std::cout << "Headers: ";
    for (std::map<std::string, std::string>::const_iterator it = conn->response_data_->headers_.begin();
         it != conn->response_data_->headers_.end(); ++it) {
        std::cout << it->first << "=" << it->second << "; ";
    }
    std::cout << std::endl;
    std::cout << "Body size: " << conn->response_data_->body_.size() << " bytes" << std::endl;
    std::cout << "=====================\n" << std::endl;

    // TEMP For now, just echo back the data on the buffer
    if (print_and_erase_buffer(conn->write_buffer_) < 0) {
        handle_error(conn);
        return;
    }
    // TEMP

    // Write the response to the client
    // TEMP - change boolean to status enum because we should close the
    // connection on some cases (like Bad Request) even if it is keep-alive
    bool writing_complete = response_writer_->write_response(conn);
    if (!writing_complete) {
        // If writing is not complete, we need to wait for next EPOLLOUT event
        return;
    }

    if (conn->is_keep_alive()) {
        conn->reset_for_keep_alive();
        update_epoll_events(conn->client_fd_, EPOLLIN);
    } else {
        close_client_connection(conn);
    }
}

void Server::handle_error(Connection* conn) {
    // Handle error
    close_client_connection(conn);
}

bool Server::setup_listener_socket() {
    // Create the listener socket
    // AF_INET for IPv4, SOCK_STREAM for TCP, SOCK_NONBLOCK for making it non
    // blocking, 0 for default protocol
    listener_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listener_fd_ < 0) {
        // Handle error
        std::cerr << "Failed to create listener socket for server "
                  << config_.server_names_[0] << "on port " << config_.port_
                  << std::endl;
        return false;
    }

    // Set socket option SO_REUSEADDR to allow reuse of the port immediately,
    // avoid TIME_WAIT after TCP close
    int opt = 1;
    if (setsockopt(listener_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
        0) {
        // Handle error
        std::cerr << "Failed to set socket options for server "
                  << config_.server_names_[0] << "on port " << config_.port_
                  << std::endl;
        close(listener_fd_);
        return false;
    }

    // TODO - allow Virtual Hosting (bind to multiple hostnames)
    // Bind to the specified port
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.port_);
    if (config_.host_ == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        addr.sin_addr.s_addr = inet_addr(config_.host_.c_str());
    }

    if (bind(listener_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        // Handle error
        std::cerr << "Failed to bind listener socket for server "
                  << config_.server_names_[0] << "on port " << config_.port_
                  << std::endl;
        close(listener_fd_);
        return false;
    }

    // Listen for incoming connections
    if (listen(listener_fd_, SOMAXCONN) < 0) {
        // Handle error
        std::cerr << "Failed to listen on socket for server "
                  << config_.server_names_[0] << "on port " << config_.port_
                  << std::endl;
        close(listener_fd_);
        return false;
    }

    return true;
}

int Server::close_timed_out_connections() {
    return conn_manager_->close_timed_out_connections();
}

bool Server::set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        // Handle error
        std::cerr << "Failed to get flags for socket (fd: " << fd << ")"
                  << "on server " << config_.server_names_[0] << "on port "
                  << config_.port_ << std::endl;
        return false;
    }

    flags |= O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) == -1) {
        // Handle error
        std::cerr << "Failed to set non-blocking mode for socket (fd: " << fd
                  << ")" << "on server " << config_.server_names_[0]
                  << "on port " << config_.port_ << std::endl;
        return false;
    }

    return true;
}

bool Server::register_epoll_events(int fd, uint32_t events) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.fd = fd;

    if (epoll_ctl(server_manager_->get_epoll_fd(), EPOLL_CTL_ADD, fd, &event) <
        0) {
        // Handle error
        std::cerr << "Failed to register fd: " << fd << "on server "
                  << config_.server_names_[0] << "on port " << config_.port_
                  << std::endl;
        return false;
    }

    return true;
}

bool Server::update_epoll_events(int fd, uint32_t events) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.fd = fd;

    if (epoll_ctl(server_manager_->get_epoll_fd(), EPOLL_CTL_MOD, fd, &event) <
        0) {
        // Handle error
        std::cerr << "Failed to up epoll events for fd: " << fd << "on server "
                  << config_.server_names_[0] << "on port " << config_.port_
                  << std::endl;
        return false;
    }

    return true;
}
