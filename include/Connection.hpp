#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "webserv.hpp"

class AHandler;
struct HttpRequest;
struct HttpResponse;
struct VirtualServer;
struct ParserContext;
class Buffer;
enum ConnectionState;

// Represents the state associated with a single client connection
struct Connection {
    //--------------------------------------
    // Constructor / Destructor
    //--------------------------------------
    Connection(WebServer* owner, int fd,
               const VirtualServer* default_virtual_server);
    ~Connection();

    //--------------------------------------
    // Core Connection Identification & I/O
    //--------------------------------------
    WebServer* const owner_server_;
    int client_fd_;
    const VirtualServer* const default_virtual_server_;
    const VirtualServer* virtual_server_;
    time_t last_activity_;

    //--------------------------------------
    // Buffers
    //--------------------------------------
    Buffer read_buffer_;
    Buffer write_buffer_;
    Buffer* decoded_buffer_;

    //--------------------------------------
    // Request/Response Data Pointers (Owned by Connection)
    //--------------------------------------
    HttpRequest* request_data_;
    HttpResponse* response_data_;

    //--------------------------------------
    // State Management
    //--------------------------------------
    ConnectionState conn_state_;
    ParserContext parser_context_;
    std::vector<IOContext*> io_contexts_;

    //--------------------------------------
    // Connection Management
    //--------------------------------------
    IOContext* add_io_context(int fd, FdType type, uint32_t events = EPOLLIN);
    void remove_io_context(IOContext* io_context);
    bool is_keep_alive() const;
    void reset_for_keep_alive();

    //--------------------------------------
    // Handler-Specific Context
    //--------------------------------------
    AHandler* active_handler_;
    const Location* location_match_;

    StaticFileContext* static_file_context_;
    FileUploadContext* file_upload_context_;
    CgiContext* cgi_context_;

   private:
    // Prevent copying
    Connection(const Connection&);
    Connection& operator=(const Connection&);

};  // struct Connection

enum ConnectionState {
    CONN_READING_REQUEST,
    CONN_GENERATING_RESPONSE,
    CONN_WRITING_RESPONSE,
    CONN_COMPLETE,
    CONN_ERROR
};

#endif  // CONNECTION_HPP