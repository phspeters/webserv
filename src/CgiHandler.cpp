#include "webserv.hpp"

CgiHandler::CgiHandler() {}

CgiHandler::~CgiHandler() {}

void CgiHandler::handle(Connection* conn) {
    // 1. Check redirect (same as StaticFileHandler)
    if (process_location_redirect(conn)) {
        return;  // Redirect response was set up, we're done
    }

    // Extract request data
    const std::string& request_uri = conn->request_data_->uri_;
    const std::string& request_method = conn->request_data_->method_;
    const Location* location = conn->location_match_;

    std::cout << "\n==== CGI HANDLER ====\n";
    std::cout << "Request URI: " << request_uri << std::endl;
    std::cout << "Request Method: " << request_method << std::endl;
    std::cout << "Matched location: " << location->path_ << std::endl;
    std::cout << "Root: " << location->root_ << std::endl;

    // NOTE - Already checked in WebServer::validate_request (Maybe keep
    // everything here?)
    // 2. Method Validation (GET or POST only)
    // if (request_method != "GET" || request_method != "POST") {
    //    conn->response_data_->status_code_ = 405;
    //    conn->response_data_->status_message_ = "Method Not Allowed";
    //    conn->response_data_->headers_["Allow"] = "GET, POST";
    //    return;
    //}

    // 3. URI Format Check (can't execute a directory)
    if (!request_uri.empty() && request_uri[request_uri.length() - 1] == '/') {
        log(LOG_ERROR, "URI is a directory, cannot execute: %s",
            request_uri.c_str());
        ErrorHandler::generate_error_response(conn, codes::BAD_REQUEST);
        return;
    }

    // Extract script information from the request (script_path_, path_info_,
    // query_string_)
    if (!extract_script_info(conn->request_data_)) {
        // Failed to extract script info, return error
        log(LOG_ERROR, "Failed to extract CGI script information");
        ErrorHandler::generate_error_response(conn,
                                              codes::INTERNAL_SERVER_ERROR);
        return;
    }

    // 4. Script Path Resolution
    script_path_ = parse_absolute_path(conn);
    // --CHECK I dont think this is necessary
    // if (script_path_.empty()) {
    //     std::cout << "Error: Failed to determine CGI script path\n";
    //     std::cout << "=====================\n" << std::endl;
    //     conn->response_data_->status_code_ = 500;
    //     conn->response_data_->status_message_ = "Internal Server Error";
    //     return;
    // }

    // 5. Script Extension Check
    bool is_valid_extension = false;
    std::string extension;
    size_t dot_pos = script_path_.find_last_of('.');
    if (dot_pos != std::string::npos) {
        extension = script_path_.substr(dot_pos + 1);
        // --CHECK - Check against allowed extensions
        if (extension == "php" || extension == "py" || extension == "sh") {
            is_valid_extension = true;
        }
    }

    if (!is_valid_extension) {
        log(LOG_ERROR, "Invalid CGI script extension: %s", extension);
        ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
        return;
    }

    // 6. Script Existence Check
    struct stat file_info;
    if (stat(script_path_.c_str(), &file_info) == -1) {
        if (errno == ENOENT) {
            log(LOG_ERROR, "CGI script '%s' not found", script_path_.c_str());
            ErrorHandler::generate_error_response(conn, codes::NOT_FOUND);
        } else {
            log(LOG_ERROR, "Failed to access CGI script: %s", strerror(errno));
            ErrorHandler::generate_error_response(conn,
                                                  codes::INTERNAL_SERVER_ERROR);
        }
        return;
    }

    // 7. Script Type Validation
    if (!S_ISREG(file_info.st_mode)) {
        log(LOG_ERROR, "CGI script '%s' is not a regular file", script_path_);
        ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
        return;
    }

    // 8. Script Permission Check
    if (!(file_info.st_mode & S_IXUSR)) {
        log(LOG_ERROR, "CGI script '%s' is not executable",
            script_path_.c_str());
        ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
        return;
    }

    // NOTE - Already checked on WebServer::validate_request (Must have valid
    // content-length or transfer-encoding: chunked) - Bring everything here?
    //// 9. Content-Length Check for POST
    // if (request_method == "POST") {
    //     // Check if Content-Length is present for POST requests
    //     if (conn->request_data_->headers_.find("Content-Length") ==
    //     conn->request_data_->headers_.end()) {
    //         std::cout << "Error: Content-Length header missing for POST
    //         request\n"; std::cout << "=====================\n" << std::endl;
    //         conn->response_data_->status_code_ = 411;
    //         conn->response_data_->status_message_ = "Length Required";
    //         return;
    //     }
    //     // Check if Content-Length exceeds max allowed size from server
    //     config size_t max_content_length = 0;
    //     // Get the virtual server's client_max_body_size_ setting
    //     if (conn->virtual_server_) {
    //         max_content_length =
    //         conn->virtual_server_->client_max_body_size_;
    //     } else if (conn->default_virtual_server_) {
    //         // Fall back to default virtual server if specific one isn't
    //         matched max_content_length =
    //         conn->default_virtual_server_->client_max_body_size_;
    //     } else {
    //         // --CHECK - Should throw an error?
    //         max_content_length = 1 * 1024 * 1024; // Default to 1MB if no
    //         server config is available at all
    //     }
    //     size_t content_length = 0;
    //     try {
    //         content_length =
    //         std::atoi(conn->request_data_->headers_["Content-Length"].c_str());
    //     } catch (...) {
    //         content_length = 0;
    //     }
    //     if (content_length > max_content_length) {
    //         std::cout << "Error: Request body too large: " << content_length
    //         << " bytes (max: "
    //                 << max_content_length << " bytes)\n";
    //         std::cout << "=====================\n" << std::endl;
    //         conn->response_data_->status_code_ = 413;
    //         conn->response_data_->status_message_ = "Request Entity Too
    //         Large"; return;
    //     }
    // }

    // ---------- EXECUTE THE CGI SCRIPT ----------

    // NOTE: Careful state management to avoid multiple forks or leaks

    // Setup pipes
    int server_to_cgi_pipe[2];  // pipe for server to write to CGI's stdin
    int cgi_to_server_pipe[2];  // pipe for CGI's stdout to be read by server
    setup_cgi_pipes(conn, server_to_cgi_pipe, cgi_to_server_pipe);

    pid_t pid = fork();
    if (pid == -1) {
        // Fork failure
        log(LOG_ERROR, "Fork error: %s", strerror(errno));
        ErrorHandler::generate_error_response(conn,
                                              codes::INTERNAL_SERVER_ERROR);
        return;
    } else if (pid == 0) {
        //// This code runs ONLY in the CHILD process
        //// 1. Close unnecessary pipe ends (parent's read/write ends)
        //// 2. Redirect stdin/stdout/stderr using dup2()
        // handle_child_pipes(conn, server_to_cgi_pipe, cgi_to_server_pipe);
        //// 3. Set up environment variables
        // setup_cgi_environment(conn);
        //// 4. Call execve() to run the CGI script
        ////    If execve fails, print error and _exit() (not exit())
        ////    _exit() is safer in a forked child as it doesn't flush stdio
        ////    buffers
        // execute_cgi_script(conn, script_path);
    } else {
        //// This code runs ONLY in the PARENT process
        //// 1. Store 'pid' (the child's PID) in the Connection object
        conn->cgi_pid_ = pid;
        //// 2. Close unnecessary pipe ends (child's read/write ends)
        handle_parent_pipes(conn, server_to_cgi_pipe, cgi_to_server_pipe);
        //// 3. If POST request, write request body to child's stdin pipe
        //// 4. Close the write end of the input pipe to signal EOF to child
        // if (conn->request_data_->method_ == "POST") {
        //     write_request_body_to_cgi(conn);
        //     close(conn->cgi_pipe_stdin_fd_);
        // }
        //// 5. Read output from child's stdout pipe
        //// 6. Call waitpid(pid, &status, 0) to wait for the child to finish
        ////    (or use non-blocking waitpid with SIGCHLD)
        // setup_signal_handler(SIGCHLD);
        // waitpid(-1, &status, WNOHANG);
        //// 7. Parse output and form response
        // parse_cgi_output(conn);
    }

    // ---------- END EXECUTE THE CGI SCRIPT ----------
}

void setup_cgi_pipes(Connection* conn, int server_to_cgi_pipe[2],
                     int cgi_to_server_pipe[2]) {
    if (pipe(server_to_cgi_pipe) == -1) {
        // Pipe creation failure
        log(LOG_ERROR, "Pipe server_to_cgi_pipe creation error: %s",
            strerror(errno));
        ErrorHandler::generate_error_response(conn,
                                              codes::INTERNAL_SERVER_ERROR);
        return;
    }
    if (pipe(cgi_to_server_pipe) == -1) {
        // Pipe creation failure
        log(LOG_ERROR, "Pipe cgi_to_server_pipe creation error: %s",
            strerror(errno));
        ErrorHandler::generate_error_response(conn,
                                              codes::INTERNAL_SERVER_ERROR);
        return;
    }
}

// TODO - Implement epoll registration
void handle_parent_pipes(Connection* conn, int server_to_cgi_pipe[2],
                         int cgi_to_server_pipe[2]) {
    // Close the read-end of the pipe to CGI's stdin
    // and write-end of the pipe from CGI's stdout
    close(server_to_cgi_pipe[0]);  // Parent closes read-end of pipe to CGI's
                                   // stdin
    conn->cgi_pipe_stdin_fd_ = server_to_cgi_pipe[1];  // Parent keeps write-end
    // NOTE - Must register this pipe with epoll for EPOLLOUT events
    close(cgi_to_server_pipe[1]);  // Parent closes write-end of pipe from CGI's
                                   // stdout
    conn->cgi_pipe_stdout_fd_ = cgi_to_server_pipe[0];  // Parent keeps read-end
    // NOTE - Must register this pipe with epoll for EPOLLIN events
}
