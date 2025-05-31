#include "webserv.hpp"

CgiHandler::CgiHandler() {}

CgiHandler::~CgiHandler() {}

void CgiHandler::handle(Connection* conn) {
    switch (conn->cgi_handler_state_) {
        case codes::CGI_HANDLER_IDLE:
            // Initial state, ready to start CGI execution
            if (!validate_cgi_request(conn)) {
                // Validation failed, error response already generated
                return;
            }
            setup_cgi_execution(conn);
            break;
        case codes::CGI_HANDLER_WRITING_TO_PIPE:
            // Write request body to CGI's stdi, should not reach here
            handle_cgi_write(conn);
            break;
        case codes::CGI_HANDLER_READING_FROM_PIPE:
            // Read from CGI's stdout
            handle_cgi_read(conn);
            break;
        case codes::CGI_HANDLER_COMPLETE:
        case codes::CGI_HANDLER_ERROR:
            conn->conn_state_ = codes::CONN_WRITING;
            break;
    }
}

bool CgiHandler::validate_cgi_request(Connection* conn) {
    // 1. Check redirect (same as StaticFileHandler)
    if (process_location_redirect(conn)) {
        return false;  // Redirect response was set up, stop processing
    }

    // Extract request data
    const std::string& request_uri = conn->request_data_->uri_;
    const std::string& request_method = conn->request_data_->method_;
    const Location* location = conn->location_match_;

    log(LOG_TRACE,
        "CGI Handler request\nRequest URI: %s\nRequest Method: %s\n Matched "
        "location: %s\nRoot: %s",
        request_uri.c_str(), request_method.c_str(), location->path_.c_str(),
        location->root_.c_str());

    // 2. Method Validation (GET or POST only)
    if (request_method != "GET" && request_method != "POST") {
        log(LOG_ERROR, "Invalid request method '%s' for CGI script",
            request_method.c_str());
        ErrorHandler::generate_error_response(conn, codes::METHOD_NOT_ALLOWED);
        conn->response_data_->headers_["Allow"] = "GET, POST";
        return false;
    }

    // 3. URI Format Check (can't execute a directory)
    if (!request_uri.empty() && request_uri[request_uri.length() - 1] == '/') {
        log(LOG_ERROR, "URI is a directory, cannot execute: %s",
            request_uri.c_str());
        ErrorHandler::generate_error_response(conn, codes::BAD_REQUEST);
        return false;
    }

    // 4. Script Path Resolution
    conn->script_path_ = parse_absolute_path(conn);
    // NOTE - Not necessary?
    if (conn->script_path_.empty()) {
        log(LOG_ERROR, "Failed to determine CGI script path for URI: %s",
            request_uri.c_str());
        ErrorHandler::generate_error_response(conn,
                                              codes::INTERNAL_SERVER_ERROR);
        return false;
    }

    // 5. Script Extension Check
    bool is_valid_extension = false;
    std::string extension;
    size_t dot_pos = conn->script_path_.find_last_of('.');
    if (dot_pos != std::string::npos) {
        extension = conn->script_path_.substr(dot_pos + 1);
        // --CHECK - Check against allowed extensions
        if (extension == "php" || extension == "py" || extension == "sh") {
            is_valid_extension = true;
        }
    }

    if (!is_valid_extension) {
        log(LOG_ERROR, "Invalid CGI script extension '%s' for script '%s'",
            extension.c_str(), conn->script_path_.c_str());
        ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
        return false;
    }

    // 6. Script Existence Check
    struct stat file_info;
    if (stat(conn->script_path_.c_str(), &file_info) == -1) {
        if (errno == ENOENT) {
            log(LOG_ERROR, "CGI script '%s' not found",
                conn->script_path_.c_str());
            ErrorHandler::generate_error_response(conn, codes::NOT_FOUND);
        } else {
            log(LOG_ERROR, "Failed to access CGI script '%s': %s",
                conn->script_path_.c_str(), strerror(errno));
            ErrorHandler::generate_error_response(conn,
                                                  codes::INTERNAL_SERVER_ERROR);
        }
        return false;
    }

    // 7. Script Type Validation
    if (!S_ISREG(file_info.st_mode)) {
        log(LOG_ERROR, "CGI script '%s' is not a regular file",
            conn->script_path_.c_str());
        ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
        return false;
    }

    // 8. Script Permission Check
    if (!(file_info.st_mode & S_IXUSR)) {  // Check for user execute permission
        log(LOG_ERROR, "CGI script '%s' is not executable",
            conn->script_path_.c_str());
        ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
        return false;
    }

    log(LOG_DEBUG, "CGI request validated for script: %s",
        conn->script_path_.c_str());
    return true;  // All checks passed
}

bool CgiHandler::setup_cgi_execution(Connection* conn) {
    // Ensure request_method is accessible
    const std::string& request_method = conn->request_data_->method_;

    // Setup pipes
    int server_to_cgi_pipe[2];  // pipe for server to write to CGI's stdin
    int cgi_to_server_pipe[2];  // pipe for CGI's stdout to be read by server
    if (!setup_cgi_pipes(conn, server_to_cgi_pipe, cgi_to_server_pipe)) {
        // ErrorHandler::generate_error_response was called in setup_cgi_pipes
        return false;
    }

    pid_t pid = fork();
    if (pid == -1) {
        // Fork failure
        close(server_to_cgi_pipe[0]);
        close(server_to_cgi_pipe[1]);
        close(cgi_to_server_pipe[0]);
        close(cgi_to_server_pipe[1]);

        log(LOG_ERROR, "Fork error: %s", strerror(errno));
        ErrorHandler::generate_error_response(conn,
                                              codes::INTERNAL_SERVER_ERROR);
        return false;
    } else if (pid == 0) {
        // This code runs ONLY in the CHILD process
        handle_child_pipes(conn, server_to_cgi_pipe, cgi_to_server_pipe);
        setup_cgi_environment(conn);
        execute_cgi_script(conn);
    } else {
        // This code runs ONLY in the PARENT process
        conn->cgi_pid_ = pid;
        if (!handle_parent_pipes(conn, server_to_cgi_pipe,
                                 cgi_to_server_pipe)) {
            ErrorHandler::generate_error_response(conn,
                                                  codes::INTERNAL_SERVER_ERROR);
            return false;
        }
        WebServer* web_server = WebServer::get_instance();
        if (request_method == "POST" && !conn->request_data_->body_.empty()) {
            conn->cgi_handler_state_ = codes::CGI_HANDLER_WRITING_TO_PIPE;
            // Register this pipe with epoll for EPOLLOUT events
            if (!web_server->register_epoll_events(conn->cgi_pipe_stdin_fd_,
                                                   EPOLLOUT)) {
                log(LOG_ERROR, "Failed to register CGI stdin pipe with epoll");
                return false;
            }
            log(LOG_DEBUG,
                "CGI: POST request, state -> WRITING_TO_PIPE for client %d, "
                "stdin_fd %d",
                conn->client_fd_, conn->cgi_pipe_stdin_fd_);
        } else {
            conn->cgi_handler_state_ = codes::CGI_HANDLER_READING_FROM_PIPE;
            // Register this pipe with epoll for EPOLLIN events
            if (!web_server->register_epoll_events(conn->cgi_pipe_stdout_fd_,
                                                   EPOLLIN)) {
                log(LOG_ERROR, "Failed to register CGI stdout pipe with epoll");
                return false;
            }
            // If it's a GET request or an empty POST, we can close the stdin
            // pipe
            if (conn->cgi_pipe_stdin_fd_ != -1) {
                web_server->get_conn_manager()->unregister_pipe(
                    conn->cgi_pipe_stdin_fd_);
                close(conn->cgi_pipe_stdin_fd_);
                conn->cgi_pipe_stdin_fd_ = -1;  // Mark as closed
                log(LOG_DEBUG,
                    "CGI: Closed stdin pipe immediately for "
                    "non-POST/empty-POST for client %d",
                    conn->client_fd_);
            }
            log(LOG_DEBUG,
                "CGI: GET or empty POST, state -> READING_FROM_PIPE for client "
                "%d, stdout_fd %d",
                conn->client_fd_, conn->cgi_pipe_stdout_fd_);
        }
    }

    return true;  // Successfully forked and parent setup initiated
}

bool CgiHandler::setup_cgi_pipes(Connection* conn, int server_to_cgi_pipe[2],
                                 int cgi_to_server_pipe[2]) {
    // Create pipes for communication between server and CGI script

    if (pipe(server_to_cgi_pipe) == -1) {
        // Pipe creation failure
        log(LOG_ERROR, "Pipe server_to_cgi_pipe creation error: %s",
            strerror(errno));
        ErrorHandler::generate_error_response(conn,
                                              codes::INTERNAL_SERVER_ERROR);
        return false;
    }

    if (pipe(cgi_to_server_pipe) == -1) {
        // Pipe creation failure
        log(LOG_ERROR, "Pipe cgi_to_server_pipe creation error: %s",
            strerror(errno));
        ErrorHandler::generate_error_response(conn,
                                              codes::INTERNAL_SERVER_ERROR);
        return false;
    }

    log(LOG_DEBUG,
        "CGI pipes created: server_to_cgi_pipe: %d and %d, cgi_to_server_pipe: "
        "%d and %d",
        server_to_cgi_pipe[0], server_to_cgi_pipe[1], cgi_to_server_pipe[0],
        cgi_to_server_pipe[1]);
    return true;
}

void CgiHandler::handle_child_pipes(Connection* conn, int server_to_cgi_pipe[2],
                                    int cgi_to_server_pipe[2]) {
    // Close the read-end of the pipe to CGI's stdin
    close(server_to_cgi_pipe[0]);
    // Close the write-end of the pipe from CGI's stdout
    close(cgi_to_server_pipe[1]);

    // Redirect stdin to the read-end of the server_to_cgi_pipe
    if (dup2(server_to_cgi_pipe[1], STDIN_FILENO) == -1) {
        log(LOG_ERROR, "Failed to redirect stdin to CGI pipe: %s",
            strerror(errno));
        _exit(EXIT_FAILURE);
    }
    // Redirect stdout to the write-end of the cgi_to_server_pipe
    if (dup2(cgi_to_server_pipe[0], STDOUT_FILENO) == -1) {
        log(LOG_ERROR, "Failed to redirect stdout to CGI pipe: %s",
            strerror(errno));
        _exit(EXIT_FAILURE);
    }
    // Redirect stderr to stdout (optional, but useful for debugging)
    if (dup2(STDOUT_FILENO, STDERR_FILENO) == -1) {
        log(LOG_ERROR, "Failed to redirect stderr to stdout: %s",
            strerror(errno));
        _exit(EXIT_FAILURE);
    }

    // Close the original pipe file descriptors in the child process
    close(server_to_cgi_pipe[1]);
    close(cgi_to_server_pipe[0]);

    /// TEMP
    (void)conn;
}

// TODO - Include any other necessary environment variables for CGI execution
// TODO - Include error handling for environment setup
void CgiHandler::setup_cgi_environment(Connection* conn) {
    // Set QUERY_STRING if available
    if (!conn->request_data_->query_string_.empty()) {
        setenv("QUERY_STRING", conn->request_data_->query_string_.c_str(), 1);
    }

    // Set REQUEST_METHOD
    setenv("REQUEST_METHOD", conn->request_data_->method_.c_str(), 1);

    // Set SCRIPT_NAME (the script path)
    setenv("SCRIPT_NAME", conn->script_path_.c_str(), 1);
}

void CgiHandler::execute_cgi_script(Connection* conn) {
    char* script_path = const_cast<char*>(conn->script_path_.c_str());

    char* const argv[] = {script_path, NULL};
    char* const envp[] = {NULL};

    // Execute the CGI script
    if (execve(script_path, argv, envp) == -1) {
        log(LOG_ERROR, "Failed to execute CGI script '%s': %s", script_path,
            strerror(errno));
        _exit(EXIT_FAILURE);  // Exit child process on exec failure
    }
}

bool CgiHandler::handle_parent_pipes(Connection* conn,
                                     int server_to_cgi_pipe[2],
                                     int cgi_to_server_pipe[2]) {
    WebServer* web_server = WebServer::get_instance();

    close(server_to_cgi_pipe[0]);  // Parent closes read-end of pipe
    conn->cgi_pipe_stdin_fd_ = server_to_cgi_pipe[1];  // Parent keeps write-end

    close(cgi_to_server_pipe[1]);  // Parent closes write-end of pipe
    conn->cgi_pipe_stdout_fd_ = cgi_to_server_pipe[0];  // Parent keeps read-end

    // Set the pipe stdin to non-blocking mode
    if (!web_server->set_non_blocking(conn->cgi_pipe_stdin_fd_)) {
        log(LOG_ERROR,
            "Failed to set CGI stdin pipe to non-blocking mode for "
            "client %d",
            conn->client_fd_);
        if (conn->cgi_pid_ > 0) {
            log(LOG_ERROR,
                "Terminating CGI child process %d due to pipe setup "
                "failure for client %d",
                conn->cgi_pid_, conn->client_fd_);
            kill(conn->cgi_pid_, SIGKILL);  // Terminate the child process
            conn->cgi_pid_ = -1;
        }
        if (conn->cgi_pipe_stdin_fd_ != -1) {
            close(conn->cgi_pipe_stdin_fd_);
            conn->cgi_pipe_stdin_fd_ = -1;  // Mark as closed
        }
        if (conn->cgi_pipe_stdout_fd_ != -1) {
            close(conn->cgi_pipe_stdout_fd_);
            conn->cgi_pipe_stdout_fd_ = -1;  // Mark as closed
        }
        return false;
    }

    // Set the pipe stdout to non-blocking mode
    if (!web_server->set_non_blocking(conn->cgi_pipe_stdout_fd_)) {
        log(LOG_ERROR,
            "Failed to set CGI stdout pipe to non-blocking mode for "
            "client %d",
            conn->client_fd_);
        if (conn->cgi_pid_ > 0) {
            log(LOG_ERROR,
                "Terminating CGI child process %d due to pipe setup "
                "failure for client %d",
                conn->cgi_pid_, conn->client_fd_);
            kill(conn->cgi_pid_, SIGKILL);  // Terminate the child process
            conn->cgi_pid_ = -1;
        }
        if (conn->cgi_pipe_stdin_fd_ != -1) {
            close(conn->cgi_pipe_stdin_fd_);
            conn->cgi_pipe_stdin_fd_ = -1;  // Mark as closed
        }
        if (conn->cgi_pipe_stdout_fd_ != -1) {
            close(conn->cgi_pipe_stdout_fd_);
            conn->cgi_pipe_stdout_fd_ = -1;  // Mark as closed
        }
        return false;
    }

    // Register the pipe with the ConnectionManager
    web_server->get_conn_manager()->register_pipe(conn->cgi_pipe_stdin_fd_,
                                                  conn);
    web_server->get_conn_manager()->register_pipe(conn->cgi_pipe_stdout_fd_,
                                                  conn);

    log(LOG_DEBUG, "Parent pipes set up for CGI: stdin_fd=%d, stdout_fd=%d",
        conn->cgi_pipe_stdin_fd_, conn->cgi_pipe_stdout_fd_);
    return true;
}

// TODO - Close fds and free resources on error
void CgiHandler::handle_cgi_write(Connection* conn) {
    WebServer* web_server = WebServer::get_instance();

    // Write request body to CGI's stdin pipe
    ssize_t bytes_written =
        write(conn->cgi_pipe_stdin_fd_, conn->request_data_->body_.data(),
              conn->request_data_->body_.size());

    if (bytes_written < 0) {
        // TODO - Close fds and free resources
        log(LOG_ERROR, "Failed to write to CGI stdin pipe: %s",
            strerror(errno));
        ErrorHandler::generate_error_response(conn,
                                              codes::INTERNAL_SERVER_ERROR);
        return;
    }

    // Clear written bytes from request body after writing
    conn->request_data_->body_.erase(
        conn->request_data_->body_.begin(),
        conn->request_data_->body_.begin() + bytes_written);

    // If all data is written, close the pipe and switch to reading state
    if (conn->request_data_->body_.empty()) {
        close(conn->cgi_pipe_stdin_fd_);
        conn->cgi_pipe_stdin_fd_ = -1;  // Mark as closed
        conn->cgi_handler_state_ =
            codes::CGI_HANDLER_READING_FROM_PIPE;  // Switch to reading state

        // Register the stdout pipe for reading
        if (!web_server->register_epoll_events(conn->cgi_pipe_stdout_fd_,
                                               EPOLLIN)) {
            // TODO - Close fds and free resources
            log(LOG_ERROR, "Failed to register CGI stdout pipe with epoll");
            ErrorHandler::generate_error_response(conn,
                                                  codes::INTERNAL_SERVER_ERROR);
            return;
        }
    } else {
        // Still have more data to write, keep the state as writing
        log(LOG_DEBUG, "Partial write to CGI stdin pipe for client %d",
            conn->client_fd_);
    }
}

// TODO - Read from CGI's stdout pipe and handle the response
void CgiHandler::handle_cgi_read(Connection* conn) {
    WebServer* web_server = WebServer::get_instance();

    // Read from CGI's stdout pipe
    char buffer[4096];  // Buffer size can be adjusted as needed
    ssize_t bytes_read =
        read(conn->cgi_pipe_stdout_fd_, buffer, sizeof(buffer));

    if (bytes_read < 0) {
        log(LOG_ERROR, "Failed to read from CGI stdout pipe: %s",
            strerror(errno));
        ErrorHandler::generate_error_response(conn,
                                              codes::INTERNAL_SERVER_ERROR);
        return;
    } else if (bytes_read == 0) {
        // EOF reached, CGI script has finished execution
        conn->cgi_handler_state_ = codes::CGI_HANDLER_COMPLETE;
        web_server->get_conn_manager()->unregister_pipe(
            conn->cgi_pipe_stdout_fd_);
        close(conn->cgi_pipe_stdout_fd_);
        conn->cgi_pipe_stdout_fd_ = -1;  // Mark as closed
        log(LOG_DEBUG, "CGI script completed for client %d", conn->client_fd_);
        conn->conn_state_ = codes::CONN_WRITING;  // Switch to writing state
        return;
    }
    // If we reach here, bytes_read > 0, meaning we have data to process
    log(LOG_DEBUG, "Read %zd bytes from CGI stdout for client %d", bytes_read,
        conn->client_fd_);

    parse_cgi_output(conn, buffer, bytes_read);
    if (conn->cgi_handler_state_ != codes::CGI_HANDLER_COMPLETE) {
        log(LOG_DEBUG,
            "CGI still processing for client %d, waiting for more data",
            conn->client_fd_);
        return;
    }

    // If we reach here, CGI response is complete
    log(LOG_DEBUG, "CGI response complete for client %d", conn->client_fd_);
    conn->conn_state_ = codes::CONN_WRITING;  // Switch to writing state
    web_server->get_conn_manager()->unregister_pipe(conn->cgi_pipe_stdout_fd_);
    close(conn->cgi_pipe_stdout_fd_);
    conn->cgi_pipe_stdout_fd_ = -1;  // Mark as closed
}

void CgiHandler::parse_cgi_output(Connection* conn, char* buffer,
                                  ssize_t bytes_read) {
    // Set the response data in the connection
    (void)bytes_read;
    (void)buffer;
    conn->cgi_handler_state_ = codes::CGI_HANDLER_COMPLETE;
}
