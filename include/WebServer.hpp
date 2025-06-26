#ifndef WEBSERVER_HPP
#define WEBSERVER_HPP

#include "webserv.hpp"

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
    ~WebServer();

    // Initialize server components and network setup
    bool init();

    // Load server configuration from the specified file
    bool parse_config_file(const std::string& filename);

    // Start the server event loop. This will block until shutdown is called.
    void run();

    // Set the running flag to false and exit the event loop
    void shutdown();

    // Public interface for epoll management
    bool add_context_to_epoll(IOContext* ctx, uint32_t events);
    bool update_context_in_epoll(IOContext* ctx, uint32_t events);
    bool remove_context_from_epoll(IOContext* ctx);

    // Getter for singleton instance
    static WebServer* get_instance() { return instance_; };

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
    std::list<VirtualServer> virtual_servers_;
    std::vector<IOContext*>
        listener_contexts_;
    std::map<int, std::vector<VirtualServer*> > listener_to_virtual_servers_;
    std::map<int, Connection*>
        active_connections_;
    volatile bool ready_;     // Flag for server readiness for event loop

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
    Connection* create_client_connection(
        int client_fd, const VirtualServer* default_virtual_server);
    void close_client_connection(Connection* conn);
    int cleanup_timed_out_connections();
    bool read_from_client_socket(Connection* conn);

    void handle_client_socket_event(Connection* conn, uint32_t event_flags);
    void handle_static_file_event(IOContext* ctx, uint32_t event_flags);
    void handle_cgi_read_event(IOContext* ctx, uint32_t event_flags);
    void handle_cgi_write_event(IOContext* ctx, uint32_t event_flags);
    void handle_file_upload_event(IOContext* ctx, uint32_t event_flags);

    bool setup_listener_sockets();
    int create_listener_socket(const std::string& host, int port);

    bool set_non_blocking(int fd);
    bool add_listener_context(int listener_fd);
    bool remove_listener_context(IOContext* ctx);

    static bool setup_signal_handlers();
    static void signal_handler(int signal);

    // Prevent copying
    WebServer(const WebServer&);
    WebServer& operator=(const WebServer&);

    //--- TODO: review these methods
    ParseStatus WebServer::process_request(Connection* conn);
    void match_host_header(Connection* conn);
    const Location* find_matching_location(const VirtualServer* virtual_server,
                                           const std::string& path) const;
    bool validate_request_location(Connection* conn);
    ConnectionState determine_body_handling_state(Connection* conn);
    bool is_cgi_extension(const std::string& request_uri) const;
    std::string get_file_extension(const std::string& uri_path) const;
    AHandler* choose_handler(Connection* conn);
    //--- TODO: review these methods
};  // class WebServer

struct IOContext {
    Connection* conn_;
    FdType type_;
    int fd_;

    IOContext(int fd, FdType type, Connection* conn)
        : fd_(fd), type_(type), conn_(conn) {}
};

enum FdType {
    FD_LISTENER,
    FD_CLIENT_SOCKET,
    FD_CGI_PIPE_READ,
    FD_CGI_PIPE_WRITE,
    FD_FILE_UPLOAD,
    FD_STATIC_FILE,
};

#endif  // WEBSERVER_HPP