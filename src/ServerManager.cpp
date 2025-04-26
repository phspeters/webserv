#include "webserv.hpp"

ServerManager* ServerManager::instance_ = NULL;

ServerManager::ServerManager() : epoll_fd_(-1), running_(false) {
    instance_ = this;
}

ServerManager::~ServerManager() {
    // Clean up servers and close epoll_fd
    cleanup_servers();
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
    }
}

bool ServerManager::init() {
    // Create epoll instance
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        // Handle error
        return false;  // Return error, don't terminate
    }

    // Set up signal handlers
    setup_signal_handlers();

    return true;  // Successfully initialized
}

void ServerManager::run() {
    running_ = true;

    // Start the event loop
    event_loop();
}

void ServerManager::shutdown() {
    running_ = false;

    // Clean up servers
    cleanup_servers();
}

Server* ServerManager::get_server_by_fd(int fd) const {
    std::map<int, Server*>::const_iterator it = fd_to_server_map_.find(fd);
    if (it == fd_to_server_map_.end()) {
        return NULL;  // Not found
    }
    return it->second;
}

bool ServerManager::add_server(Server* server) {
    // Get server's listener fd
    int listener_fd = server->get_listener_fd();

    // Add to epoll and our map
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = listener_fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listener_fd, &event) < 0) {
        // Handle error
        return false;
    }

    fd_to_server_map_[listener_fd] = server;
    servers_.push_back(server);

    return true;
}

// TEMP
bool ServerManager::parse_config_file(const std::string& filename) {
    (void)filename;
    // Parse the configuration file and initialize servers
    // This is a placeholder for actual parsing logic
    // For now, we just create a single server with default settings
    ServerConfig config;
    config.port_ = 8080;                // Example port
    config.server_name_ = "localhost";  // Example server name

    Server* server = new Server(config, this);
    if (!server->init()) {
        // Handle error
        delete server;
        return false;
    }

    if (!add_server(server)) {
        // Handle error
        delete server;
        return false;
    }

    return true;
}

void ServerManager::unregister_fd(int fd) {
    // Remove from epoll instance
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, NULL) < 0) {
        // Handle error
        return;
    }

    // Remove from mapping
    fd_to_server_map_.erase(fd);
}

// TODO
void ServerManager::setup_signal_handlers() {}

void ServerManager::signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        get_instance()->shutdown();
    }
}

void ServerManager::event_loop() {
    struct epoll_event events[MAX_EPOLL_EVENTS];

    while (running_) {
        int ready_events = epoll_wait(epoll_fd_, events, MAX_EPOLL_EVENTS, -1);

        for (int i = 0; i < ready_events; i++) {
            int fd = events[i].data.fd;

            // Find which server is responsible for this fd
            Server* server = get_server_by_fd(fd);
            if (!server) {
                // Handle error - fd not found in server map
                continue;
            }

            // Let the server handle its own event
            if (fd == server->get_listener_fd()) {
                // Listening socket event - server accepts new connection
                server->accept_new_connection(epoll_fd_, fd_to_server_map_);
            } else {
                // Client socket event
                server->handle_client_event(fd, events[i].events);
            }
        }
    }
}

void ServerManager::cleanup_servers() {
    for (std::vector<Server*>::iterator it = servers_.begin();
         it != servers_.end(); ++it) {
        delete *it;
    }

    servers_.clear();
    fd_to_server_map_.clear();
}

// TODO
bool ServerManager::check_server_health() {
    // Placeholder for health check logic
    return true;
}

// TODO
void ServerManager::handle_error(const std::string& message) {
    std::cerr << "Error: " << message << std::endl;
}
