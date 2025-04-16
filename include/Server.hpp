#ifndef SERVER_HPP
#define SERVER_HPP

#include <string>  // For config file path
#include <vector>

// Required C/System headers
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// Include config directly as it's owned/used extensively
#include "ServerConfig.hpp"

// Forward declarations of owned components and used types
class ConnectionManager;
class RequestParser;
class Router;
class ResponseWriter;
class StaticFileHandler;
class CgiHandler;
// Add other handlers...
class Connection;  // Used in method signatures

// Main server class - orchestrates setup, event loop, and component
// interactions.
class Server {
   public:
    explicit Server(const std::string& config_filename);
    ~Server();  // Cleans up owned components and sockets

    // Initialize server components and network setup. Returns false on error.
    bool init();

    // Start the main event loop. Blocks until stop() is called or error occurs.
    void run();

    // Signal the server to stop gracefully. Can be called from another thread
    // or signal handler (carefully).
    void stop();

   private:
    //--------------------------------------
    // Server State & Configuration
    //--------------------------------------
    ServerConfig config;    // Loaded server configuration
    int listener_fd;        // FD for the listening socket
    int epoll_fd;           // FD for the epoll instance
    volatile bool running;  // Controls the main event loop

    //--------------------------------------
    // Owned Components (Composition)
    //--------------------------------------
    ConnectionManager* conn_manager;
    RequestParser* request_parser;
    Router* router;
    ResponseWriter* response_writer;
    // Handler instances (owned by Server)
    StaticFileHandler* static_file_handler;
    CgiHandler* cgi_handler;
    // Add other handler instances...

    //--------------------------------------
    // Internal Methods
    //--------------------------------------
    bool setup_listener_socket();  // Sets up listener_fd
    bool setup_epoll();            // Sets up epoll_fd and adds listener

    void event_loop();  // The main loop calling epoll_wait

    void handle_listener_event();  // Accepts new connections
    void handle_client_event(
        struct epoll_event& event);  // Handles client socket events

    void handle_read(Connection* conn);   // Handles readable client socket
    void handle_write(Connection* conn);  // Handles writable client socket
    void handle_error(Connection* conn,
                      const char* context);  // Handles client socket error

    bool set_non_blocking(int fd);  // Utility

    // Prevent copying
    Server(const Server&);
    Server& operator=(const Server&);

    // Epoll event buffer
    std::vector<struct epoll_event> epoll_events;
    static const int MAX_EPOLL_EVENTS = 64;

};  // class Server

#endif  // SERVER_HPP
