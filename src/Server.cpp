#include "webserv.hpp"

Server::Server(const ServerConfig& config, ServerManager* manager)
    : manager_(manager),
      config_(config),
      listener_fd_(-1),
      initialized_(false),
      conn_manager_(NULL) {}
/*
request_parser_(NULL),
router_(NULL),
response_writer_(NULL),
static_file_handler_(NULL),
cgi_handler_(NULL) {}
*/

Server::~Server() {
    // Clean up owned components
    delete conn_manager_;
    /*
     delete request_parser_;
     delete router_;
     delete response_writer_;
     delete static_file_handler_;
     delete cgi_handler_;
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
    /*
    // Initialize components
    conn_manager_ = new ConnectionManager(config_);
    request_parser_ = new RequestParser(config_);
    response_writer_ = new ResponseWriter(config_);

    // Initialize handlers
    static_file_handler_ = new StaticFileHandler(config_, *response_writer_);
    cgi_handler_ = new CgiHandler(config_, *response_writer_);

    // Initialize the router with the handlers
    router_ = new Router(config_, static_file_handler_, cgi_handler_);
    */

    // Set up the listener socket and epoll instance
    if (!setup_listener_socket()) {
        // Handle error
        return false;
    }

    initialized_ = true;

    return true;
}

void Server::accept_new_connection(int epoll_fd,
                                   std::map<int, Server*>& fd_map) {
    int client_fd = accept(listener_fd_, NULL, NULL);
    if (client_fd < 0) {
        // Handle error
        return;
    }
    // Set non-blocking
    if (!set_non_blocking(client_fd)) {
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

    // Register with epoll
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = client_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event)) {
        // Handle error
        close(client_fd);
        return;
    }

    // Add to ServerManager's map
    fd_map[client_fd] = this;
}

void Server::close_client_connection(Connection* conn) {
    if (!conn) {
        return;
    }

    int client_fd = conn->client_fd_;

    // 1. First unregister from epoll (must happen before socket closure)
    manager_->unregister_fd(client_fd);

    // 2. Then let connection manager handle the rest
    conn_manager_->close_connection(conn);
}

void Server::handle_client_event(int client_fd, uint32_t events) {
    Connection* conn = conn_manager_->get_connection(client_fd);
    if (!conn) {
        // Handle error
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

// TODO
void Server::handle_read(Connection* conn) {
    (void)conn;
    // TODO read and parse request

    // Switch to write mode
    set_socket_mode(conn->client_fd_, EPOLLOUT);
}

// TODO
void Server::handle_write(Connection* conn) {
    (void)conn;
    // TODO write response

    // After writing response, for keep-alive:
    if (conn->is_keep_alive()) {
        conn->reset_for_keep_alive();
        set_socket_mode(conn->client_fd_, EPOLLIN);
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
    listener_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_fd_ < 0) {
        // Handle error
        return false;
    }

    // Set socket option SO_REUSEADDR to allow reuse of the port immediately,
    // avoid TIME_WAIT after TCP close
    int opt = 1;
    if (setsockopt(listener_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
        0) {
        // Handle error
        close(listener_fd_);
        return false;
    }

    if (!set_non_blocking(listener_fd_)) {
        // Handle error
        close(listener_fd_);
        return false;
    }

    // Bind to the specified port
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(config_.port_);

    if (bind(listener_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        // Handle error
        close(listener_fd_);
        return false;
    }

    // Listen for incoming connections
    if (listen(listener_fd_, SOMAXCONN) < 0) {
        // Handle error
        close(listener_fd_);
        return false;
    }

    return true;
}

bool Server::set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        // Handle error
        return false;
    }

    flags |= O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) == -1) {
        // Handle error
        return false;
    }

    return true;
}

bool Server::set_socket_mode(int fd, uint32_t mode) {
    struct epoll_event event;
    event.events = mode | EPOLLET;  // Always edge-triggered
    event.data.fd = fd;

    if (epoll_ctl(manager_->get_epoll_fd(), EPOLL_CTL_MOD, fd, &event) < 0) {
        // Handle error
        return false;
    }
    return true;
}
