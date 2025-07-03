#ifndef WEBSERVER_HPP
#define WEBSERVER_HPP

#include "common.hpp"

class CgiHandler;
class RequestParser;
class MultipartParser;
class ResponseWriter;
class StaticFileHandler;
class FileUploadHandler;
class FileDeleteHandler;
struct Connection;
struct VirtualServer;
struct IOContext;

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

    // Utility functions
    bool set_non_blocking(int fd);

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
    std::vector<IOContext*> listener_contexts_;
    std::map<int, std::vector<VirtualServer*> > listener_to_virtual_servers_;
    std::map<int, Connection*> active_connections_;
    volatile bool ready_;  // Flag for server readiness for event loop

    //--------------------------------------
    // Owned Components (Composition)
    //--------------------------------------
    RequestParser* request_parser_;
    MultipartParser* multipart_parser_;
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
    int cleanup_timed_out_connections();

    void accept_new_connection(int listener_fd);
    Connection* create_client_connection(
        int client_fd, const VirtualServer* default_virtual_server);
    void close_client_connection(Connection* conn);

    void handle_client_socket_event(IOContext* ctx, uint32_t event_flags);
    bool read_from_client_socket(Connection* conn);
    ParseStatus handle_request_parsing(Connection* conn);
    ParseStatus process_request(Connection* conn);
    void match_host_header(Connection* conn);
    const Location* match_location(const VirtualServer* virtual_server,
                                   const std::string& path) const;
    ParseStatus validate_version(Connection* conn);
    ParseStatus validate_method(Connection* conn);
    ParseStatus validate_body_handling(Connection* conn);
    AHandler* choose_handler(Connection* conn);
    ParserState determine_body_handling_state(Connection* conn);
    bool handle_keep_alive(Connection* conn);

    void handle_static_file_event(IOContext* ctx, uint32_t event_flags);
    void handle_cgi_read_event(IOContext* ctx, uint32_t event_flags);
    void handle_cgi_write_event(IOContext* ctx, uint32_t event_flags);
    void handle_file_upload_event(IOContext* ctx, uint32_t event_flags);

    bool setup_listener_sockets();
    int create_listener_socket(const std::string& host, int port);

    bool add_listener_context(int listener_fd);
    bool remove_listener_context(IOContext* ctx);

    static bool setup_signal_handlers();
    static void signal_handler(int signal);

    // Prevent copying
    WebServer(const WebServer&);
    WebServer& operator=(const WebServer&);

};  // class WebServer

#endif  // WEBSERVER_HPP