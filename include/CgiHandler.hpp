#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
class AHandler;
struct HttpRequest;
enum CgiHandlerState;
struct CgiContext;

// Handles requests by executing CGI scripts.
class CgiHandler : public AHandler {
   public:
    // Constructor takes dependencies
    CgiHandler();
    virtual ~CgiHandler();

    // Implementation of the handle method for CGI.
    // - Sets up environment variables.
    // - Creates pipes for stdin/stdout.
    // - Forks and executes the CGI script.
    // - Sets up Connection state (PID, pipe FDs, CGI state).
    // - Registers pipe FDs with epoll (done by Server based on Connection
    // state).
    // - Uses ResponseWriter potentially later when CGI output headers are
    // parsed.
    virtual void handle(Connection* conn);

    // CGI often requires handling specific read/write events on pipes
    void handle_cgi_read(
        Connection* conn);  // Called when CGI stdout pipe is readable
    void handle_cgi_write(
        Connection* conn);  // Called when CGI stdin pipe is writable

   private:
    // Helper methods for setting up environment, parsing CGI headers etc. go in
    // .cpp
    bool validate_cgi_request(Connection* conn);
    bool setup_cgi_execution(Connection* conn);
    bool setup_cgi_pipes(Connection* conn, int server_to_cgi_pipe[2],
                         int cgi_to_server_pipe[2]);
    void handle_child_pipes(int server_to_cgi_pipe[2],
                            int cgi_to_server_pipe[2]);
    std::vector<char*> create_cgi_envp(Connection* conn);
    void execute_cgi_script(Connection* conn, char** envp);
    bool handle_parent_pipes(Connection* conn, int server_to_cgi_pipe[2],
                             int cgi_to_server_pipe[2]);
    void parse_cgi_output(
        Connection* conn);  // Parses CGI headers/body separation
    void finalize_cgi_response(Connection* conn);
    void finalize_cgi_error(Connection* conn, ResponseStatus status);
    bool set_status_line(Connection* conn);
    void cleanup_cgi_resources(Connection* conn);

    // Prevent copying
    CgiHandler(const CgiHandler&);
    CgiHandler& operator=(const CgiHandler&);
};  // class CgiHandler

struct CgiContext {
    // CGI State (Only relevant if active_handler is CgiHandler)
    CgiHandlerState cgi_handler_state_;  // State of the CGI handler (if active)
    pid_t cgi_pid_;           // Process ID of the CGI script (-1 if none)
    int cgi_pipe_stdin_fd_;   // FD for writing request body TO CGI (-1 if none)
    int cgi_pipe_stdout_fd_;  // FD for reading response FROM CGI (-1 if none)
    std::string cgi_script_path_;  // Path to the CGI script
    std::vector<std::string>
        cgi_envp_;  // Environment variables for the CGI script execution
    Buffer cgi_input_buffer_;   // Buffer for writing CGI input
    Buffer cgi_output_buffer_;  // Buffer for reading CGI output

    CgiContext()
        : cgi_handler_state_(CGI_IDLE),
          cgi_pid_(-1),
          cgi_pipe_stdin_fd_(-1),
          cgi_pipe_stdout_fd_(-1) {}
};

enum CgiHandlerState {
    CGI_IDLE,
    CGI_WRITING_TO_PIPE,    // Writing request body to CGI stdin
    CGI_READING_FROM_PIPE,  // Reading response from CGI stdout
    CGI_HEADERS_PARSED,     // Headers parsed, waiting for body
    CGI_COMPLETE,           // CGI script finished
    CGI_ERROR               // Error occurred during CGI handling
};

#endif  // CGIHANDLER_HPP
