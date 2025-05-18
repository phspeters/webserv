#include "webserv.hpp"

ConnectionManager::ConnectionManager() {}

ConnectionManager::~ConnectionManager() {
    // Destructor implementation
    for (std::map<int, Connection*>::iterator it = active_connections_.begin();
         it != active_connections_.end(); ++it) {
        delete it->second;
    }
}

Connection* ConnectionManager::create_connection(
    int client_fd, const VirtualServer* default_virtual_server) {
    // Create a new Connection object and store it in the map
    try {
        Connection* conn = new Connection(client_fd, default_virtual_server);
        active_connections_[client_fd] = conn;
        return conn;
    } catch (const std::exception& e) {
        // Handle error
        std::cerr << "Failed to create connection for client (fd: " << client_fd
                  << ")" << "on " << default_virtual_server->host_ << ":"
                  << default_virtual_server->port_ << std::endl;
        return NULL;
    }
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
    std::cerr << "Connection not found for client (fd: " << client_fd << ")"
              << std::endl;
    return NULL;  // Not found
}

int ConnectionManager::close_timed_out_connections() {
    // Check for timeouts and close connections if necessary
    int closed = 0;
    for (std::map<int, Connection*>::iterator it = active_connections_.begin();
         it != active_connections_.end();) {
        Connection* conn = it->second;
        it++;
        if (is_timed_out(conn)) {
            std::cerr << "Connection timed out (fd: " << conn->client_fd_ << ")"
                      << std::endl;
            close_connection(conn);
            closed++;
        }
    }
    return closed;
}

bool ConnectionManager::is_timed_out(Connection* conn) {
    // Check if the connection is timed out
    time_t current_time = time(NULL);
    return (current_time - conn->last_activity_) > http_limits::TIMEOUT;
}

size_t ConnectionManager::get_active_connection_count() const {
    return active_connections_.size();
}
