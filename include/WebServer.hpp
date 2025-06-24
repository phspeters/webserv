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
class FileDeleteHandler;
struct IOContext;
enum FdType;

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

    // Public interface for epoll management
    void add_fd_to_epoll(IOContext* ctx, uint32_t events);
    void update_fd_in_epoll(IOContext* ctx, uint32_t events);
    void remove_fd_from_epoll(IOContext* ctx);

    // Getter for instance
    static WebServer* get_instance() { return instance_; };
    // Getter for the ConnectionManager
    ConnectionManager* get_conn_manager() const { return conn_manager_; }

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
    std::map<int, IOContext*>
        listener_contexts_;  // FDs for the listening sockets
    std::map<int, VirtualServer*> listener_to_default_server_;
    std::map<int, std::map<std::string, std::vector<VirtualServer*> > >
        port_to_hosts_;
    volatile bool ready_;  // Flag for server readiness for event loop
    std::map<int, Connection*>
        active_connections_;  // Storage for active connections, keyed by their
                              // file descriptor.
    //--------------------------------------
    // Owned Components (Composition)
    //--------------------------------------
    RequestParser* request_parser_;
    ResponseWriter* response_writer_;
    //// Handler instances
    StaticFileHandler* static_file_handler_;
    CgiHandler* cgi_handler_;
    FileUploadHandler* file_upload_handler_;
    FileDeleteHandler* file_delete_handler_;

    // Make singleton instance for signal handling
    static WebServer* instance_;

    //--------------------------------------
    // Internal Methods
    //--------------------------------------
    void event_loop();
    void accept_new_connection(int listener_fd);
    void close_client_connection(Connection* conn);
    int cleanup_timed_out_connections();

    void handle_client_socket_event(Connection* conn, uint32_t event_flags);
    void handle_static_file_event(IOContext* ctx, uint32_t event_flags);
    void handle_cgi_read_event(IOContext* ctx, uint32_t event_flags);
    void handle_cgi_write_event(IOContext* ctx, uint32_t event_flags);
    void handle_file_upload_event(IOContext* ctx, uint32_t event_flags);

    ParseStatus WebServer::process_request(Connection* conn);
    void match_host_header(Connection* conn);
    const Location* find_matching_location(const VirtualServer* virtual_server,
                                           const std::string& path) const;
    bool validate_request_location(Connection* conn);
    ConnectionState determine_body_handling_state(Connection* conn);
    bool is_cgi_extension(const std::string& request_uri) const;
    std::string get_file_extension(const std::string& uri_path) const;
    AHandler* choose_handler(Connection* conn);
    void close_client_connection(Connection* conn);

    bool set_non_blocking(int fd);
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

struct IOContext {
    Connection* conn_;  // Pointer to the associated connection
    FdType type_;       // Type of file descriptor (e.g., client, pipe)
    int fd_;            // File descriptor for the I/O operation

    IOContext(Connection* conn, FdType type, int fd)
        : conn_(conn), type_(type), fd_(fd) {}
};

enum FdType {
    FD_LISTENER,        // Listener socket file descriptor
    FD_CLIENT_SOCKET,   // Client socket file descriptor
    FD_CGI_PIPE_READ,   // Pipe file descriptor (e.g., for CGI)
    FD_CGI_PIPE_WRITE,  // Pipe file descriptor (e.g., for CGI)
    FD_FILE_UPLOAD,     // File descriptor for file upload
    FD_STATIC_FILE,     // File descriptor for static file serving
};

#endif  // WEBSERVER_HPP