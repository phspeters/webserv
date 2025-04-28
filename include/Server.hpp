#ifndef SERVER_HPP
#define SERVER_HPP

#include "webserv.hpp"

// Forward declarations of owned components and used types
class CgiHandler;
class Connection;
class ConnectionManager;
class RequestParser;
class ResponseWriter;
class Router;
class ServerConfig;
class ServerManager;
class StaticFileHandler;
// Add other handlers...

// Main server class - orchestrates setup, event loop, and component
// interactions.
class Server {
   public:
    explicit Server(const ServerConfig& config,
                    ServerManager* manager = NULL);  // Constructor with config
    ~Server();  // Cleans up owned components and sockets

    // Initialize server components and network setup. Returns false on error.
    bool init();

    // Accepts new connections on the listener socket and registers them with
    // epoll.
    void accept_new_connection(int shared_epoll_fd,
                               std::map<int, Server*>& fd_map);

    void close_client_connection(Connection* conn);

    void handle_client_event(int fd,
                             uint32_t event);  // Handles client socket events

    // Getters for internal state
    bool is_ready() const { return initialized_; }
    int get_listener_fd() const { return listener_fd_; }  // Get listener fd
    const ServerConfig& get_config() const { return config_; }  // Get config
    unsigned short get_port() const { return config_.port_; }
    std::string get_server_name() const { return config_.server_name_; }

    // Add other getters as needed...

   private:
    //--------------------------------------
    // Server State & Configuration
    //--------------------------------------
    ServerManager* manager_;  // Reference to owner
    ServerConfig config_;     // Loaded server configuration
    int listener_fd_;         // FD for the listening socket;
    bool initialized_;        // Flag for server running state

    //--------------------------------------
    // Owned Components (Composition)
    //--------------------------------------
    ConnectionManager* conn_manager_;
    // RequestParser* request_parser_;
    // Router* router_;
    // ResponseWriter* response_writer_;
    //// Handler instances (owned by Server)
    // StaticFileHandler* static_file_handler_;
    // CgiHandler* cgi_handler_;
    //// Add other handler instances...

    //--------------------------------------
    // Internal Methods
    //--------------------------------------
    void handle_read(Connection* conn);   // Handles readable client socket
    void handle_write(Connection* conn);  // Handles writable client socket
    void handle_error(Connection* conn);  // Handles client socket error

    bool setup_listener_socket();                 // Sets up listener_fd
    bool set_non_blocking(int fd);                // Utility
    bool set_socket_mode(int fd, uint32_t mode);  // Utility

    // Prevent copying
    Server(const Server&);
    Server& operator=(const Server&);

};  // class Server

#endif  // SERVER_HPP
