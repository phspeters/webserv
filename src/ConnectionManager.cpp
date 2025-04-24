#include "webserv.hpp"

ConnectionManager::ConnectionManager(const ServerConfig& config)
    : config(config) {
    // Constructor implementation
}

ConnectionManager::~ConnectionManager() {
    // Destructor implementation
    for (std::map<int, Connection*>::iterator it = active_connections.begin();
         it != active_connections.end(); ++it) {
        delete it->second;
    }

    active_connections.clear();
}

Connection* ConnectionManager::create_connection(int client_fd) {
    // Create a new Connection object and store it in the map
    Connection* conn = new Connection(client_fd, &config);
    active_connections[client_fd] = conn;
    return conn;
}

void ConnectionManager::close_connection(int client_fd) {
    // Find the connection in the map
    std::map<int, Connection*>::iterator it =
        active_connections.find(client_fd);
    if (it != active_connections.end()) {
        // Close and delete the connection
        delete it->second;
        active_connections.erase(it);
    }
}

void ConnectionManager::close_connection(Connection* conn) {
    // Find the connection in the map
    std::map<int, Connection*>::iterator it =
        active_connections.find(conn->client_fd);
    if (it != active_connections.end()) {
        // Close and delete the connection
        delete it->second;
        active_connections.erase(it);
    }
}

Connection* ConnectionManager::get_connection(int client_fd) {
    // Find the connection in the map
    std::map<int, Connection*>::iterator it =
        active_connections.find(client_fd);
    if (it != active_connections.end()) {
        return it->second;
    }
    return NULL;  // Not found
}

// TODO
int ConnectionManager::check_timeouts() { return 0; }

size_t ConnectionManager::get_active_connection_count() const {
    return active_connections.size();
}
