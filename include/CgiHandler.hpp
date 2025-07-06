#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

#include "common.hpp"

class AHandler;
struct Connection;
struct CgiContext;

class CgiHandler : public AHandler {
   public:
    CgiHandler();
    virtual ~CgiHandler();

    virtual Result check_permissions(Connection* conn);
    virtual Result setup_handler(Connection* conn);
    virtual void cleanup_handler(Connection* conn);

    Result handle_cgi_read(Connection* conn);
    Result handle_cgi_write(Connection* conn);

   private:
    bool setup_cgi_execution(Connection* conn);
    bool setup_cgi_pipes(Connection* conn, int server_to_cgi_pipe[2],
                         int cgi_to_server_pipe[2]);
    void handle_child_pipes(int server_to_cgi_pipe[2],
                            int cgi_to_server_pipe[2]);
    std::vector<char*> create_cgi_envp(Connection* conn);
    void execute_cgi_script(Connection* conn, char** envp);
    bool handle_parent_pipes(Connection* conn, int server_to_cgi_pipe[2],
                             int cgi_to_server_pipe[2]);
    Result parse_cgi_output(Connection* conn);
    bool set_status_line(Connection* conn);

    // Prevent copying
    CgiHandler(const CgiHandler&);
    CgiHandler& operator=(const CgiHandler&);
};  // class CgiHandler

// TODO: Assess if we should change pipe to socketpair
struct CgiContext {
    pid_t cgi_pid_;
    int cgi_pipe_stdin_fd_;
    int cgi_pipe_stdout_fd_;
    std::string cgi_script_path_;
    std::vector<std::string> cgi_envp_;
    Buffer cgi_output_buffer_;

    CgiContext()
        : cgi_pid_(-1), cgi_pipe_stdin_fd_(-1), cgi_pipe_stdout_fd_(-1) {}
};

#endif  // CGIHANDLER_HPP
