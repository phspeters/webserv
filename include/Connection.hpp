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

// Represents the state associated with a single client connection
struct Connection {
    //--------------------------------------
    // Constructor / Destructor
    //--------------------------------------
    Connection(int fd, const VirtualServer* default_virtual_server);
    ~Connection();  // Cleans up owned resources (Request, Response, FDs)

    //--------------------------------------
    // Core Connection Identification & I/O
    //--------------------------------------
    int client_fd_;  // File descriptor for the client socket
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
    codes::ConnectionState conn_state_;  // Current state of the connection
    ParserContext parser_context_;       // Context for parsing requests

    //--------------------------------------
    // Connection Management
    //--------------------------------------
    void reset_for_keep_alive();  // Resets state for handling another request
    bool is_keep_alive() const;   // Checks if the connection is keep-alive

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

#endif  // CONNECTION_HPP