#include "webserv.hpp"

ConnectionManager::ConnectionManager(const ServerConfig& config)
    : config_(config) {
    // Constructor implementation
}

ConnectionManager::~ConnectionManager() {
    // Destructor implementation
    for (std::map<int, Connection*>::iterator it = active_connections_.begin();
         it != active_connections_.end(); ++it) {
        delete it->second;
    }
}

Connection* ConnectionManager::create_connection(int client_fd) {
    // Create a new Connection object and store it in the map
    Connection* conn = new Connection(client_fd, config_);
    active_connections_[client_fd] = conn;
    return conn;
}

void ConnectionManager::close_connection(int client_fd) {
    // Find the connection in the map
    std::map<int, Connection*>::iterator it =
        active_connections_.find(client_fd);
    if (it != active_connections_.end()) {
        // Close and delete the connection
        delete it->second;
        active_connections_.erase(it);
    }
}

void ConnectionManager::close_connection(Connection* conn) {
    // Find the connection in the map
    std::map<int, Connection*>::iterator it =
        active_connections_.find(conn->client_fd_);
    if (it != active_connections_.end()) {
        // Close and delete the connection
        delete it->second;
        active_connections_.erase(it);
    }
}

Connection* ConnectionManager::get_connection(int client_fd) {
    // Find the connection in the map
    std::map<int, Connection*>::iterator it =
        active_connections_.find(client_fd);
    if (it != active_connections_.end()) {
        return it->second;
    }
    return NULL;  // Not found
}

// TODO
int ConnectionManager::check_timeouts() { return 0; }

size_t ConnectionManager::get_active_connection_count() const {
    return active_connections_.size();
}
