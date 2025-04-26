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

    // Cleanly stop handling requests and prepare for shutdown.
    // Called by ServerManager during graceful shutdown.
    void stop();

    // Accepts new connections on the listener socket and registers them with
    // epoll.
    void accept_new_connection(int shared_epoll_fd,
                               std::map<int, Server*>& fd_map);

    void handle_client_event(int fd,
                             uint32_t event);  // Handles client socket events

    // Getters for internal state
    bool is_ready() const { return _initialized; }
    int get_listener_fd() const { return _listener_fd; }  // Get listener fd
    const ServerConfig& get_config() const { return _config; }  // Get config
    unsigned short get_port() const { return _config.port; }
    std::string get_server_name() const { return _config.server_name; }

    // Add other getters as needed...

   private:
    //--------------------------------------
    // Server State & Configuration
    //--------------------------------------
    ServerManager* _manager;  // Reference to owner
    ServerConfig _config;     // Loaded server configuration
    int _listener_fd;         // FD for the listening socket;
    bool _initialized;        // Flag for server running state

    //--------------------------------------
    // Owned Components (Composition)
    //--------------------------------------
    ConnectionManager* _conn_manager;
    // RequestParser* _request_parser;
    // Router* _router;
    // ResponseWriter* _response_writer;
    //// Handler instances (owned by Server)
    // StaticFileHandler* _static_file_handler;
    // CgiHandler* _cgi_handler;
    //// Add other handler instances...

    //--------------------------------------
    // Internal Methods
    //--------------------------------------
    void handle_read(Connection* conn);   // Handles readable client socket
    void handle_write(Connection* conn);  // Handles writable client socket
    void handle_error(Connection* conn);  // Handles client socket error

    bool setup_listener_socket();   // Sets up listener_fd
    bool set_non_blocking(int fd);  // Utility
	bool set_socket_mode(int fd, uint32_t mode);  // Utility

    // Prevent copying
    Server(const Server&);
    Server& operator=(const Server&);

};  // class Server

#endif  // SERVER_HPP
