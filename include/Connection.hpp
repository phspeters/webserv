#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "webserv.hpp"

// Forward declarations
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
    ~Connection();  // Cleans up owned resources (Request, Response, FDs)

    //--------------------------------------
    // Core Connection Identification & I/O
    //--------------------------------------
    WebServer* const owner_server_;  // Pointer to the owning WebServer instance
    int client_fd_;                  // File descriptor for the client socket
    const VirtualServer* const
        default_virtual_server_;           // Pointer to default virtual server
    const VirtualServer* virtual_server_;  // Pointer to virtual server matching
                                           // the Host header
    time_t
        last_activity_;  // Timestamp of last read/write activity (for timeouts)

    //--------------------------------------
    // Buffers
    //--------------------------------------
    Buffer read_buffer_;   // Buffer for incoming data from client
    Buffer write_buffer_;  // Buffer for outgoing data to client
    Buffer*
        decoded_buffer_;  // Buffer for decoded data (e.g., from chunked body)

    //--------------------------------------
    // Request/Response Data Pointers (Owned by Connection)
    //--------------------------------------
    HttpRequest* request_data_;  // Pointer to the parsed request info (NULL
                                 // until allocated)
    HttpResponse*
        response_data_;  // Pointer to the response info (NULL until allocated)

    //--------------------------------------
    // State Management
    //--------------------------------------
    ConnectionState conn_state_;    // Current state of the connection
    ParserContext parser_context_;  // Context for parsing requests
    std::vector<IOContext*>
        io_contexts_;  // List of I/O contexts associated with this connection

    //--------------------------------------
    // Connection Management
    //--------------------------------------
    void reset_for_keep_alive();  // Resets state for handling another request
    bool is_keep_alive() const;   // Checks if the connection is keep-alive
    void add_io_context(
        IOContext* io_context);  // Adds an I/O context for this connection
    void remove_io_context(
        IOContext* io_context);  // Removes an I/O context for this connection

    //--------------------------------------
    // Handler-Specific Context
    //--------------------------------------
    AHandler* active_handler_;        // Pointer to the currently active handler
    const Location* location_match_;  // Best matching location for the request

    StaticFileContext*
        static_file_context_;  // Context for static file handling
    FileUploadContext*
        file_upload_context_;  // Context for file upload handling
    CgiContext* cgi_context_;  // Context for CGI handling (if active_handler is
                               // CgiHandler)

   private:
    // Prevent copying
    Connection(const Connection&);
    Connection& operator=(const Connection&);

};  // struct Connection

enum ConnectionState {
    CONN_READING_REQUEST,      // Parsing the request
    CONN_GENERATING_RESPONSE,  // Generating response headers and body
    CONN_WRITING_RESPONSE,     // Writing response to the client
    CONN_COMPLETE,             // Request processing complete (keep-alive ready)
    CONN_ERROR                 // An error occurred during processing
};

#endif  // CONNECTION_HPP