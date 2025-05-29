#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
class AHandler;
struct HttpRequest;

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
    void handle_child_pipes(Connection* conn, int server_to_cgi_pipe[2],
                            int cgi_to_server_pipe[2]);
    void setup_cgi_environment(Connection* conn);
    void execute_cgi_script(Connection* conn);
    bool handle_parent_pipes(Connection* conn, int server_to_cgi_pipe[2],
                             int cgi_to_server_pipe[2]);
    void parse_cgi_output(
        Connection* conn);  // Parses CGI headers/body separation

    // Prevent copying
    CgiHandler(const CgiHandler&);
    CgiHandler& operator=(const CgiHandler&);

    bool extract_script_info(HttpRequest* req);

};  // class CgiHandler

#endif  // CGIHANDLER_HPP
