#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "webserv.hpp"

// Forward declarations
class IHandler;
struct HttpRequest;
struct HttpResponse;
struct ServerConfig;
class Server;

// Represents the state associated with a single client connection
struct Connection {
    //--------------------------------------
    // Constructor / Destructor
    //--------------------------------------
    Connection(int fd, const ServerConfig& config);
    ~Connection();  // Cleans up owned resources (Request, Response, FDs)

    //--------------------------------------
    // Core Connection Identification & I/O
    //--------------------------------------
    int client_fd_;  // File descriptor for the client socket
    const ServerConfig&
        server_config_;  // Pointer to server configuration/context (read-only)
    time_t
        last_activity_;  // Timestamp of last read/write activity (for timeouts)

    //--------------------------------------
    // Buffers
    //--------------------------------------
    std::vector<char> read_buffer_;   // Buffer for incoming data from client
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
    // State Management (Simplified - reflects overall stage)
    //--------------------------------------
    enum ConnectionState {
        CONN_READING,     // Waiting for/reading request data
        CONN_PROCESSING,  // Request received, handler is processing
        CONN_WRITING,     // Handler generated response, sending data
        CONN_CGI_EXEC,    // Special state for active CGI execution
        CONN_ERROR        // Connection encountered an error
    };
    ConnectionState state_;

    //--------------------------------------
    // Connection Management
    //--------------------------------------
    void reset_for_keep_alive();  // Resets state for handling another request

    // Utility methods
    bool is_readable() const;  // Checks if the connection is readable
    bool is_writable() const;  // Checks if the connection is writable
    bool is_error() const;     // Checks if the connection is in error state
    bool is_keep_alive()
        const;  // Checks if the connection should be kept alive

    //--------------------------------------
    // Handler Association
    //--------------------------------------
    IHandler* active_handler_ptr_;  // Pointer to the handler responsible (set
                                    // by Router)

    //--------------------------------------
    // Handler-Specific State (Example for CGI - could be a union or void*)
    //--------------------------------------
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
