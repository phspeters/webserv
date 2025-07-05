#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "common.hpp"

class AHandler;
class Buffer;
class WebServer;
struct HttpRequest;
struct HttpResponse;
struct VirtualServer;
struct CgiContext;
struct MultipartContext;
struct ParserContext;
struct WriterContext;
struct FileUploadContext;
struct StaticFileContext;
struct IOContext;

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

    //--------------------------------------
    // Request/Response Data Pointers (Owned by Connection)
    //--------------------------------------
    HttpRequest* request_data_;
    HttpResponse* response_data_;

    //--------------------------------------
    // State Management
    //--------------------------------------
    HttpStatus status_;
    ConnectionState conn_state_;
    ParserContext parser_context_;
    MultipartContext multipart_context_;
    WriterContext writer_context_;
    std::vector<IOContext*>
        io_contexts_;  // TODO: change to map<FdType, IOContext*> ??

    //--------------------------------------
    // Connection Management
    //--------------------------------------
    IOContext* add_io_context(int fd, FdType type, uint32_t events = EPOLLIN);
    void remove_io_context(IOContext* io_context);
    bool is_keep_alive() const;
    bool is_fatal_error_status(int status) const;
    void reset_for_keep_alive();

    //--------------------------------------
    // Handler-Specific Context
    //--------------------------------------
    AHandler* active_handler_;
    const Location* location_match_;
    bool is_asynchronous_;

    StaticFileContext* static_file_context_;
    FileUploadContext* file_upload_context_;
    CgiContext* cgi_context_;

   private:
    // Prevent copying
    Connection(const Connection&);
    Connection& operator=(const Connection&);

};  // struct Connection

struct IOContext {
    Connection* conn_;
    FdType type_;
    int fd_;

    IOContext(int fd, FdType type, Connection* conn)
        : conn_(conn), type_(type), fd_(fd) {}
};

#endif  // CONNECTION_HPP