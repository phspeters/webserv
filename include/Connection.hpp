#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "webserv.hpp"

// Forward declarations
class IHandler;
class HttpRequest;
class HttpResponse;
struct ServerConfig;
class Server;

// Represents the state associated with a single client connection
struct Connection {
    //--------------------------------------
    // Core Connection Identification & I/O
    //--------------------------------------
    int client_fd;  // File descriptor for the client socket
    const ServerConfig*
        _server_config;  // Pointer to server configuration/context (read-only)
    time_t
        last_activity;  // Timestamp of last read/write activity (for timeouts)
    bool close_scheduled;  // Flag to signal connection should be closed by
                           // manager

    //--------------------------------------
    // Buffers
    //--------------------------------------
    std::vector<char> read_buffer;   // Buffer for incoming data from client
    std::vector<char> write_buffer;  // Buffer for outgoing data to client
    size_t write_buffer_offset;  // How much of the write_buffer has been sent

    //--------------------------------------
    // Request/Response Data Pointers (Owned by Connection)
    //--------------------------------------
    HttpRequest* request_data;  // Pointer to the parsed request info (NULL
                                // until allocated)
    HttpResponse*
        response_data;  // Pointer to the response info (NULL until allocated)

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
    ConnectionState state;

    //--------------------------------------
    // Handler Association
    //--------------------------------------
    IHandler* active_handler_ptr;  // Pointer to the handler responsible (set by
                                   // Router)

    //--------------------------------------
    // Handler-Specific State (Example for CGI - could be a union or void*)
    //--------------------------------------
    // CGI State (Only relevant if active_handler is CgiHandler)
    pid_t cgi_pid;           // Process ID of the CGI script (-1 if none)
    int cgi_pipe_stdin_fd;   // FD for writing request body TO CGI (-1 if none)
    int cgi_pipe_stdout_fd;  // FD for reading response FROM CGI (-1 if none)
    // Add more CGI state flags as needed (e.g., headers parsed, body
    // writing/reading state)

    // Static File State (Only relevant if active_handler is StaticFileHandler)
    int static_file_fd;                // FD of the file being sent (-1 if none)
    off_t static_file_offset;          // Current position within the file
    size_t static_file_bytes_to_send;  // Total bytes to send from file

    //--------------------------------------
    // Keep-Alive State
    //--------------------------------------
    bool keep_alive;  // Flag indicating if connection should be kept open after
                      // request

    //--------------------------------------
    // Constructor / Destructor
    //--------------------------------------
    Connection(int fd = -1, const ServerConfig* _config = NULL);
    ~Connection();  // Cleans up owned resources (Request, Response, FDs)

    void resetForKeepAlive();  // Resets state for handling another request

    void set_server(
        Server* server);  // Sets the server context for this connection

    // Utility methods
    bool is_readable() const;  // Checks if the connection is readable
    bool is_writable() const;  // Checks if the connection is writable

    void close();  // Closes the connection and cleans up resources

   private:
    // Prevent copying
    Connection(const Connection&);
    Connection& operator=(const Connection&);

};  // struct Connection

#endif  // CONNECTION_HPP
