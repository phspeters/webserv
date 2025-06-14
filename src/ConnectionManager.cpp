#include "webserv.hpp"

ConnectionManager::ConnectionManager() {}

ConnectionManager::~ConnectionManager() {
    // Destructor implementation
    for (std::map<int, Connection*>::iterator it = active_connections_.begin();
         it != active_connections_.end(); ++it) {
        delete it->second;
    }

    log(LOG_TRACE, "ConnectionManager resources cleaned up");
}

Connection* ConnectionManager::create_connection(
    int client_fd, const VirtualServer* default_virtual_server) {
    // Create a new Connection object and store it in the map
    try {
        Connection* conn = new Connection(client_fd, default_virtual_server);
        active_connections_[client_fd] = conn;
        log(LOG_INFO, "Created new connection for client (fd: %i) on %s:%d",
            client_fd, default_virtual_server->host_.c_str(),
            default_virtual_server->port_);
        return conn;
    } catch (const std::exception& e) {
        log(LOG_ERROR, "Failed to create connection for client (fd: %i): %s",
            client_fd, e.what());
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
        log(LOG_INFO, "Closed connection for client (fd: %i)", client_fd);
        return;
    }

    log(LOG_FATAL, "Connection not found for socket '%i'", client_fd);
}

void ConnectionManager::close_connection(Connection* conn) {
    // Find the connection in the map
    std::map<int, Connection*>::iterator it =
        active_connections_.find(conn->client_fd_);
    if (it != active_connections_.end()) {
        // Close and delete the connection
        delete it->second;
        active_connections_.erase(it);
        log(LOG_INFO, "Closed connection for client (fd: %i)",
            conn->client_fd_);
        return;
    }

    log(LOG_FATAL, "Connection not found for socket '%i'", conn->client_fd_);
}

Connection* ConnectionManager::get_connection(int client_fd) {
    // Find the connection in the map
    std::map<int, Connection*>::iterator it =
        active_connections_.find(client_fd);
    if (it != active_connections_.end()) {
        log(LOG_DEBUG, "Retrieved connection for client (fd: %i)", client_fd);
        return it->second;
    }

    // If not found, check active pipes
    std::map<int, Connection*>::iterator pipe_it =
        active_pipes_.find(client_fd);
    if (pipe_it != active_pipes_.end()) {
        log(LOG_DEBUG, "Retrieved pipe (fd: %i) for client (fd: %i)",
            pipe_it->first, client_fd);
        return pipe_it->second;
    }

    log(LOG_FATAL, "Connection not found for client (fd: %i)", client_fd);
    return NULL;  // Not found
}

int ConnectionManager::close_timed_out_connections() {
    int closed = 0;
    time_t current_time = time(NULL);

    std::map<int, Connection*>::iterator it = active_connections_.begin();
    while (it != active_connections_.end()) {
        Connection* conn = it->second;

        if (conn &&
            (current_time - conn->last_activity_) > http_limits::TIMEOUT) {
            log(LOG_WARNING,
                "Connection (fd: %d) timed out after %ld seconds, closing",
                conn->client_fd_, http_limits::TIMEOUT);

            int fd_to_close = conn->client_fd_;
            ++it;
            close_connection(fd_to_close);
            closed++;
        } else {
            ++it;
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

void ConnectionManager::register_pipe(int pipe_fd, Connection* conn) {
    // Register a pipe with the connection manager
    active_pipes_[pipe_fd] = conn;
    log(LOG_INFO, "Registered pipe (fd: %i) for connection (fd: %i)", pipe_fd,
        conn->client_fd_);
}

void ConnectionManager::unregister_pipe(int pipe_fd) {
    // Unregister a pipe from the connection manager
    std::map<int, Connection*>::iterator it = active_pipes_.find(pipe_fd);
    if (it != active_pipes_.end()) {
        WebServer::unregister_epoll_events(pipe_fd);
        active_pipes_.erase(it);
        log(LOG_INFO, "Unregistered pipe (fd: %i)", pipe_fd);
    } else {
        log(LOG_WARNING, "Pipe (fd: %i) not found for unregistration", pipe_fd);
    }
}
