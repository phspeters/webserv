#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "webserv.hpp"

// Forward declarations
class AHandler;
struct HttpRequest;
struct HttpResponse;
struct VirtualServer;

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
        default_virtual_server_;     // Pointer to default virtual server
    VirtualServer* virtual_server_;  // Pointer to virtual server matching the
                                     // Host header
    time_t
        last_activity_;  // Timestamp of last read/write activity (for timeouts)

    //--------------------------------------
    // Buffers
    //--------------------------------------
    std::vector<char> read_buffer_;   // Buffer for incoming data from client
    size_t chunk_remaining_bytes_;    // Remaining bytes in the current chunk
    std::vector<char> write_buffer_;  // Buffer for outgoing data to client
    size_t write_buffer_offset_;  // How much of the write_buffer has been sent

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
    codes::ParserState parser_state_;    // Current state of the parser
    codes::WriterState writer_state_;    // Current state of the writer
    codes::ParseStatus parse_status_;  // Status of the last parsing attempt

    //--------------------------------------
    // Connection Management
    //--------------------------------------
    void reset_for_keep_alive();  // Resets state for handling another request

    // Connection state checks
    bool is_readable() const;
    bool is_processing() const;
    bool is_writable() const;
    bool is_error() const;
    bool is_keep_alive() const;

    //--------------------------------------
    // Handler-Specific State (Example for CGI - could be a union or void*)
    //--------------------------------------
    AHandler* active_handler_;  // Pointer to the currently active handler

    // CGI State (Only relevant if active_handler is CgiHandler)
    pid_t cgi_pid_;           // Process ID of the CGI script (-1 if none)
    int cgi_pipe_stdin_fd_;   // FD for writing request body TO CGI (-1 if none)
    int cgi_pipe_stdout_fd_;  // FD for reading response FROM CGI (-1 if none)
    // Add more CGI state flags as needed (e.g., headers parsed, body
    // writing/reading state)

    // Static File State (Only relevant if active_handler is StaticFileHandler)
    int static_file_fd_;        // FD of the file being sent (-1 if none)
    off_t static_file_offset_;  // Current position within the file
    size_t static_file_bytes_to_send_;  // Total bytes to send from file

   private:
    // Prevent copying
    Connection(const Connection&);
    Connection& operator=(const Connection&);

};  // struct Connection

#endif  // CONNECTION_HPP
