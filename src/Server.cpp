#include "webserv.hpp"

Server::Server(const ServerConfig& config, ServerManager* manager)
    : manager_(manager),
      config_(config),
      listener_fd_(-1),
      initialized_(false),
      conn_manager_(NULL),
      request_parser_(NULL) {}
/*
router_(NULL),
response_writer_(NULL),
static_file_handler_(NULL),
cgi_handler_(NULL),
file_upload_handler_ {}
*/

Server::~Server() {
    // Clean up owned components
    delete conn_manager_;
    delete request_parser_;
    /*
     delete router_;
     delete response_writer_;
     delete static_file_handler_;
     delete cgi_handler_;
     delete file_upload_handler_;
     */

    // Close listener socket if it's open
    if (listener_fd_ != -1) {
        // Unregister from epoll first
        manager_->unregister_fd(listener_fd_);
        // Then close the socket
        close(listener_fd_);
        listener_fd_ = -1;
    }
}

bool Server::init() {
    // Initialize components
    conn_manager_ = new ConnectionManager(config_);
    request_parser_ = new RequestParser(config_);
    /*
    response_writer_ = new ResponseWriter(config_);

    // Initialize handlers
    static_file_handler_ = new StaticFileHandler(config_, *response_writer_);
    cgi_handler_ = new CgiHandler(config_, *response_writer_);
    file_upload_handler_ = new FileUploadHandler(config_, *response_writer_);

    // Initialize the router with the handlers
    router_ = new Router(config_, static_file_handler_, cgi_handler_,
                          file_upload_handler_);
    */

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
    manager_->unregister_fd(client_fd);

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

// TODO - Temporary printing data implementation
void Server::handle_read(Connection* conn) {
    // Resize the read buffer to accommodate incoming data
    size_t original_size = conn->read_buffer_.size();
    conn->read_buffer_.resize(original_size + 4096);

    // Read data from the client socket
    ssize_t bytes_read =
        read(conn->client_fd_, &conn->read_buffer_[original_size], 4096);

    if (bytes_read > 0) {
        // Resize the buffer to the actual size read
        conn->read_buffer_.resize(original_size + bytes_read);

        // Parse the request
        conn->request_data_->parse_status_ = request_parser_->parse(conn);

        // TEMP - Print request to console
        std::cout << "\n==== INCOMING REQUEST (fd: " << conn->client_fd_ << ", "
                  << bytes_read << " bytes) ====\n\n";
        std::cout << "Parse status: " << conn->request_data_->parse_status_
                  << std::endl;
        std::cout << "method: " << conn->request_data_->method_ << std::endl;
        std::cout << "uri: " << conn->request_data_->uri_ << std::endl;
        std::cout << "version: " << conn->request_data_->version_ << std::endl;
        std::cout << "headers: " << std::endl;
        for (std::map<std::string, std::string>::const_iterator it =
                 conn->request_data_->headers_.begin();
             it != conn->request_data_->headers_.end(); ++it) {
            std::cout << "  " << it->first << ": " << it->second << std::endl;
        }
        std::cout << "body: " << std::endl;
        std::cout.write(conn->request_data_->body_.data(),
                        conn->request_data_->body_.size());
        std::cout << "\n====================================\n" << std::endl;

        // TEMP - Calling router and handler
        // Check if the request parsing is complete // PARSE_SUCCESS = 0
        if (conn->request_data_->parse_status_ == 0) {
            // Create a Router instance
            Router router(config_);
            // Route the request to the appropriate handler
            AHandler* handler = router.route(conn->request_data_);
            // If a handler was found, handle the request
            if (handler) {
                handler->handle(conn);
                // delete handler;  // Don't forget to clean up the handler
            } else {
                std::cerr << "No handler found for the request" << std::endl;
            }
        }

        update_epoll_events(conn->client_fd_, EPOLLOUT);
    } else if (bytes_read == 0) {
        // Connection closed by client
        std::cout << "Client disconnected (fd: " << conn->client_fd_ << ")"
                  << std::endl;
        handle_error(conn);
    } else {
        // Read error (bytes_read < 0)
        std::cout << "Read error on socket (fd: " << conn->client_fd_ << ")"
                  << std::endl;
        handle_error(conn);
    }
}

// TODO
void Server::handle_write(Connection* conn) {
    // TODO write response

    // TEMP For now, just echo back the data
    std::cout << "Writing response to client (fd: " << conn->client_fd_ << ")"
              << std::endl;
    ssize_t bytes_written = write(conn->client_fd_, conn->write_buffer_.data(),
                                  conn->write_buffer_.size());
    if (bytes_written < 0) {
        // Handle error
        std::cerr << "Write error on socket (fd: " << conn->client_fd_ << ")"
                  << "on server " << config_.server_names_[0] << "on port "
                  << config_.port_ << std::endl;
        handle_error(conn);
        return;
    }

    // After writing response, for keep-alive:
    if (conn->is_keep_alive()) {
        conn->reset_for_keep_alive();
        update_epoll_events(conn->client_fd_, EPOLLIN);
    } else {
        // Close connection if not keep-alive
        close_client_connection(conn);
    }
}

void Server::handle_error(Connection* conn) {
    // Handle error
    close_client_connection(conn);
}

bool Server::setup_listener_socket() {
    // Create the listener socket
    // AF_INET for IPv4, SOCK_STREAM for TCP, 0 for default protocol
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

    if (epoll_ctl(manager_->get_epoll_fd(), EPOLL_CTL_ADD, fd, &event) < 0) {
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

    if (epoll_ctl(manager_->get_epoll_fd(), EPOLL_CTL_MOD, fd, &event) < 0) {
        // Handle error
        std::cerr << "Failed to up epoll events for fd: " << fd << "on server "
                  << config_.server_names_[0] << "on port " << config_.port_
                  << std::endl;
        return false;
    }

    return true;
}
