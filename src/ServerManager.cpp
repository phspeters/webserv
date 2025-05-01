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
        std::cerr << "Failed to create epoll instance" << std::endl;
        return false;  // Return error, don't terminate
    }

    // Set up signal handlers
    setup_signal_handlers();
    std::cout << "ServerManager initialized successfully" << std::endl;
    return true;  // Successfully initialized
}

void ServerManager::run() {
    running_ = true;

    // Start the event loop
    std::cout << "ServerManager is ready and waiting for connections"
              << std::endl;
    event_loop();
}

void ServerManager::shutdown() { running_ = false; }

Server* ServerManager::get_server_by_fd(int fd) const {
    std::map<int, Server*>::const_iterator it = fd_to_server_map_.find(fd);
    if (it == fd_to_server_map_.end()) {
        return NULL;  // Not found
    }
    return it->second;
}

bool ServerManager::register_server(Server* server) {
    if (server->is_ready() == false) {
        // Handle error
        std::cerr << "Server is not initialized and cannot be registered"
                  << std::endl;
        return false;
    }
    // Get server's listener fd
    int listener_fd = server->get_listener_fd();

    // Add to epoll and our map
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = listener_fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listener_fd, &event) < 0) {
        // Handle error
        std::cerr << "Failed to register on epoll server "
                  << server->get_server_name() << "on port "
                  << server->get_port() << std::endl;
        return false;
    }

    fd_to_server_map_[listener_fd] = server;
    servers_.push_back(server);

    return true;
}

bool ServerManager::parse_config_file(const std::string& filename) {
    std::ifstream file(filename.c_str());
    if (!file.is_open()) {
        std::cerr << "Error: Could not open configuration file: " << filename
                  << std::endl;
        return false;
    }

    std::string line;
    std::vector<ServerConfig> configs;
    while (std::getline(file, line)) {
        line = trim(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        // Look for server block
        if (line == "server {" ||
            (line.find("server") == 0 && line.find("{") != std::string::npos)) {
            // Create a new server config
            ServerConfig config;
            // Parse the server block
            if (ServerConfig::parseServerBlock(file, config)) {
                configs.push_back(config);
                // Print the parsed configuration
                std::cout << "\n===== PARSED SERVER CONFIGURATION =====\n"
                          << std::endl;
                config.print();
                std::cout << "\n========================================\n"
                          << std::endl;
            } else {
                std::cerr << "Error parsing server block" << std::endl;
            }
        }
    }
    for (std::vector<ServerConfig>::iterator it = configs.begin();
         it != configs.end(); it++) {
        // Create and initialize a new server
        Server* server = new Server(*it, this);
        if (!server->init()) {
            // Handle error
            std::cerr << "Failed to initialize server "
                      << server->get_server_name() << "on port "
                      << server->get_port() << std::endl;
            delete server;
            return false;
        }

        if (!register_server(server)) {
            // Handle error
            delete server;
            return false;
        }
    }
    return !servers_.empty();
}

void ServerManager::unregister_fd(int fd) {
    // Remove from epoll instance
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, NULL) < 0) {
        // Handle error
        std::cerr << "Failed to unregister fd " << fd << " from epoll"
                  << std::endl;
        return;
    }

    // Remove from mapping
    fd_to_server_map_.erase(fd);
}

void ServerManager::setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) < 0) {
        // Handle error
        std::cerr << "Failed to set up SIGINT handler" << std::endl;
        return;
    }

    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        // Handle error
        std::cerr << "Failed to set up SIGTERM handler" << std::endl;
        return;
    }
}

void ServerManager::signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        get_instance()->shutdown();
        std::cout << "Received shutdown signal. Exiting..." << std::endl;
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
                std::cerr << "Unknown fd " << fd << " in epoll event"
                          << std::endl;
                continue;
            }

            // Let the server handle its own event
            if (fd == server->get_listener_fd()) {
                // Listening socket event - server accepts new connection
                std::cout << "New connection on server "
                          << server->get_server_name() << " on port "
                          << server->get_port() << std::endl;
                server->accept_new_connection(epoll_fd_, fd_to_server_map_);
            } else {
                // Client socket event
                std::cout << "Client event on server "
                          << server->get_server_name() << " on port "
                          << server->get_port() << std::endl;
                server->handle_client_event(fd, events[i].events);
            }
        }
    }
}

void ServerManager::cleanup_servers() {
    for (std::vector<Server*>::iterator it = servers_.begin();
         it != servers_.end(); ++it) {
        delete *it;  // Clean up server instance
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
