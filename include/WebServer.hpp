#ifndef WEBSERVER_HPP
#define WEBSERVER_HPP

#include "webserv.hpp"

// Forward declarations of owned components and used types
class CgiHandler;
struct Connection;
struct ConnectionManager;
class RequestParser;
class ResponseWriter;
struct VirtualServer;
class StaticFileHandler;
class FileUploadHandler;

// Main server class - orchestrates setup and event loop
class WebServer {
   public:
    WebServer();
    ~WebServer();  // Cleans up owned components and sockets

    // Initialize server components and network setup. Returns false on error.
    bool init();

    // Load server configuration from the specified file. Returns false on
    // error.
    bool parse_config_file(const std::string& filename);

    // Start the server event loop. This will block until shutdown is called.
    void run();

    // Set the running flag to false and exit the event loop
    void shutdown();

    // Getter for instance
    static WebServer* get_instance() { return instance_; };
    // Getter for the ConnectionManager
    ConnectionManager* get_conn_manager() const { return conn_manager_; }

    bool set_non_blocking(int fd);
    bool register_epoll_events(int fd, uint32_t events = EPOLLIN);
    bool update_epoll_events(int fd, uint32_t mode);

   private:
    //--------------------------------------
    // Epoll Management
    //--------------------------------------
    int epoll_fd_;
    std::vector<struct epoll_event> epoll_events_;
    static const int MAX_EPOLL_EVENTS = 1024;

    //--------------------------------------
    // WebServer State & Configuration
    //--------------------------------------
    std::list<VirtualServer> virtual_servers_;  // Loaded server configurations
    std::vector<int> listener_fds_;             // FDs for the listening sockets
    std::map<int, VirtualServer*> listener_to_default_server_;
    std::map<int, std::map<std::string, std::vector<VirtualServer*> > >
        port_to_hosts_;
    volatile bool ready_;  // Flag for server readiness for event loop

    //--------------------------------------
    // Owned Components (Composition)
    //--------------------------------------
    ConnectionManager* conn_manager_;
    RequestParser* request_parser_;
    ResponseWriter* response_writer_;
    //// Handler instances
    StaticFileHandler* static_file_handler_;
    CgiHandler* cgi_handler_;
    FileUploadHandler* file_upload_handler_;

    // Make singleton instance for signal handling
    static WebServer* instance_;

    //--------------------------------------
    // Internal Methods
    //--------------------------------------
    void event_loop();
    int cleanup_timed_out_connections();
    void accept_new_connection(int listener_fd);
    void handle_connection_event(int client_fd, uint32_t event);

    void handle_read(Connection* conn);
    void handle_write(Connection* conn);
    void handle_error(Connection* conn);

    void match_host_header(Connection* conn);
    const Location* find_matching_location(const VirtualServer* virtual_server,
                                           const std::string& path) const;
    bool is_cgi_extension(const std::string &request_uri) const;
    bool validate_request_location(Connection* conn);
    AHandler* choose_handler(Connection* conn);
    void close_client_connection(Connection* conn);

    bool setup_listener_sockets();
    bool create_listener_socket(
        const std::string& host, int port,
        std::map<std::string, std::vector<VirtualServer*> >& hosts);
    void remove_listener_socket(int fd);

    static bool setup_signal_handlers();
    static void signal_handler(int signal);

    // Prevent copying
    WebServer(const WebServer&);
    WebServer& operator=(const WebServer&);

};  // class WebServer

std::string get_file_extension(const std::string& uri_path);

#endif  // WEBSERVER_HPP