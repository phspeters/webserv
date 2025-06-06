#ifndef CONNECTIONMANAGER_HPP
#define CONNECTIONMANAGER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
struct VirtualServer;

// Manages the lifecycle of active Connection objects.
struct ConnectionManager {
   public:
    ConnectionManager();
    ~ConnectionManager();  // Deletes all managed Connection objects

    // Creates a new Connection object for the given FD.
    // Returns pointer to the new Connection, or NULL on failure.
    // The ConnectionManager OWNS the returned Connection object.
    Connection* create_connection(int client_fd,
                                  const VirtualServer* default_virtual_server);

    // Closes and deletes the Connection object associated with the FD.
    // Handles cleanup of resources owned by the Connection.
    // Safe to call even if fd is not managed.
    void close_connection(int client_fd);

    // Closes and deletes the Connection object directly via pointer.
    void close_connection(Connection* conn);

    // Retrieves a pointer to the Connection object for a given FD.
    // Returns NULL if the FD is not managed.
    Connection* get_connection(int client_fd);

    // Iterates through connections and closes those inactive beyond timeout.
    // Returns the number of connections closed due to timeout.
    int close_timed_out_connections();

    bool is_timed_out(Connection* conn);

    // Get the number of active connections
    size_t get_active_connection_count() const;

    void register_pipe(int pipe_fd, Connection* conn);
    void unregister_pipe(int pipe_fd);

   private:
    // Storage for active connections, keyed by their file descriptor.
    // ConnectionManager owns the Connection objects pointed to.
    std::map<int, Connection*> active_connections_;
    std::map<int, Connection*> active_pipes_;  // For managing pipes (e.g., CGI)

    // Prevent copying
    ConnectionManager(const ConnectionManager&);
    ConnectionManager& operator=(const ConnectionManager&);

};  // struct ConnectionManager

#endif  // CONNECTIONMANAGER_HPP
