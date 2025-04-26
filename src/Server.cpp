#include "webserv.hpp"

Server::Server(const ServerConfig& config, ServerManager* manager)
    : _manager(manager),
      _config(config),
      _listener_fd(-1),
      _initialized(false),
      _conn_manager(NULL) {}
/*
_request_parser(NULL),
_router(NULL),
_response_writer(NULL),
_static_file_handler(NULL),
_cgi_handler(NULL) {}
*/

Server::~Server() {
    // Clean up owned components
    delete _conn_manager;
    /*
     delete _request_parser;
     delete _router;
     delete _response_writer;
     delete _static_file_handler;
     delete _cgi_handler;
     */

    // Close sockets if they are open
    if (_listener_fd != -1) {
        close(_listener_fd);
    }
}

bool Server::init() {
    /*
    // Initialize components
    _conn_manager = new ConnectionManager(_config);
    _request_parser = new RequestParser(_config);
    _response_writer = new ResponseWriter(_config);

    // Initialize handlers
    _static_file_handler = new StaticFileHandler(_config, *_response_writer);
    _cgi_handler = new CgiHandler(_config, *_response_writer);

    // Initialize the router with the handlers
    _router = new Router(_config, _static_file_handler, _cgi_handler);
    */

    // Set up the listener socket and epoll instance
    if (!setup_listener_socket()) {
        // Handle error
        return false;
    }

    _initialized = true;

    return true;
}

void Server::stop() {}

void Server::accept_new_connection(int epoll_fd,
                                   std::map<int, Server*>& fd_map) {
    int client_fd = accept(_listener_fd, NULL, NULL);
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
    Connection* conn = _conn_manager->create_connection(client_fd);
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

void Server::handle_client_event(int client_fd, uint32_t events) {
    Connection* conn = _conn_manager->get_connection(client_fd);
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
    set_socket_mode(conn->client_fd, EPOLLOUT);
}

// TODO
void Server::handle_write(Connection* conn) {
    (void)conn;
    // TODO write response

    // After writing response, for keep-alive:
    if (conn->keep_alive) {
		conn->reset_for_keep_alive();
        set_socket_mode(conn->client_fd, EPOLLIN);
    } else {
        _manager->unregister_fd(conn->client_fd);
		_conn_manager->close_connection(conn);
    }
}

void Server::handle_error(Connection* conn) {
	// Handle error
    _manager->unregister_fd(conn->client_fd);
    _conn_manager->close_connection(conn);
}

bool Server::setup_listener_socket() {
    // Create the listener socket
    // AF_INET for IPv4, SOCK_STREAM for TCP, 0 for default protocol
    _listener_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (_listener_fd < 0) {
        // Handle error
        return false;
    }

    // Set socket option SO_REUSEADDR to allow reuse of the port immediately,
    // avoid TIME_WAIT after TCP close
    int opt = 1;
    if (setsockopt(_listener_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
        0) {
        // Handle error
        close(_listener_fd);
        return false;
    }

    if (!set_non_blocking(_listener_fd)) {
        // Handle error
        close(_listener_fd);
        return false;
    }

    // Bind to the specified port
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(_config.port);

    if (bind(_listener_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        // Handle error
        close(_listener_fd);
        return false;
    }

    // Listen for incoming connections
    if (listen(_listener_fd, SOMAXCONN) < 0) {
        // Handle error
        close(_listener_fd);
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

    if (epoll_ctl(_manager->get_epoll_fd(), EPOLL_CTL_MOD, fd, &event) < 0) {
        // Handle error
        return false;
    }
    return true;
}
