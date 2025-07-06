#include "common.hpp"

CgiHandler::CgiHandler() : AHandler() {}

CgiHandler::~CgiHandler() {}

Result CgiHandler::handle(Connection* conn) {
    Result result = check_permissions(conn);
    if (result != COMPLETE) {
        return result;  // Error or redirect occurred
    }

    // Setup pipes
    int server_to_cgi_pipe[2];
    int cgi_to_server_pipe[2];
    if (!setup_cgi_pipes(conn, server_to_cgi_pipe, cgi_to_server_pipe)) {
        conn->status_ = INTERNAL_SERVER_ERROR;
        return ERROR;
    }

    const std::string& request_method = conn->request_data_->method_;
    conn->cgi_context_ = new CgiContext();

    pid_t pid = fork();
    if (pid == -1) {
        // Fork failure
        close(server_to_cgi_pipe[0]);
        close(server_to_cgi_pipe[1]);
        close(cgi_to_server_pipe[0]);
        close(cgi_to_server_pipe[1]);

        log(LOG_ERROR, "Fork error: %s", strerror(errno));
        conn->status_ = INTERNAL_SERVER_ERROR;
        return ERROR;
    } else if (pid == 0) {
        handle_child_pipes(server_to_cgi_pipe, cgi_to_server_pipe);
        std::vector<char*> envp = create_cgi_envp(conn);
        execute_cgi_script(conn, envp.data());
    } else {
        conn->cgi_context_->cgi_pid_ = pid;
        if (!handle_parent_pipes(conn, server_to_cgi_pipe,
                                 cgi_to_server_pipe)) {
            conn->status_ = INTERNAL_SERVER_ERROR;
            return ERROR;
        }

        if (request_method == "POST" &&
            !conn->request_data_->body_buffer_.empty()) {
            IOContext* stdin_ctx =
                conn->add_io_context(conn->cgi_context_->cgi_pipe_stdin_fd_,
                                     FD_CGI_PIPE_WRITE, EPOLLOUT);

            if (!stdin_ctx) {
                log(LOG_ERROR,
                    "Failed to register CGI stdin pipe with epoll for client "
                    "%d",
                    conn->client_fd_);
                conn->status_ = INTERNAL_SERVER_ERROR;
                return ERROR;
            }

            log(LOG_DEBUG,
                "CGI: POST request, state -> WRITING_TO_PIPE for client %d, "
                "stdin_fd %d",
                conn->client_fd_, conn->cgi_context_->cgi_pipe_stdin_fd_);
        } else {
            if (conn->cgi_context_->cgi_pipe_stdin_fd_ != -1) {
                close(conn->cgi_context_->cgi_pipe_stdin_fd_);
                conn->cgi_context_->cgi_pipe_stdin_fd_ = -1;
                log(LOG_DEBUG,
                    "CGI: Closed stdin pipe immediately for "
                    "non-POST/empty-POST for client %d",
                    conn->client_fd_);
            }

            IOContext* stdout_ctx =
                conn->add_io_context(conn->cgi_context_->cgi_pipe_stdout_fd_,
                                     FD_CGI_PIPE_READ, EPOLLIN);

            if (!stdout_ctx) {
                log(LOG_ERROR,
                    "Failed to register CGI stdout pipe with epoll for client "
                    "%d",
                    conn->client_fd_);
                conn->status_ = INTERNAL_SERVER_ERROR;
                return ERROR;
            }

            log(LOG_DEBUG,
                "CGI: GET or empty POST, state -> READING_FROM_PIPE for client "
                "%d, stdout_fd %d",
                conn->client_fd_, conn->cgi_context_->cgi_pipe_stdout_fd_);
        }
    }

    log(LOG_DEBUG, "CgiHandler: Setup complete for client_fd %d",
        conn->client_fd_);

    return COMPLETE;
}

Result CgiHandler::check_permissions(Connection* conn) {
    log(LOG_DEBUG, "CgiHandler: Checking permissions for client_fd %d",
        conn->client_fd_);

    // Check for location-level redirect first (takes precedence over CGI)
    if (process_location_redirect(conn)) {
        log(LOG_DEBUG,
            "StaticFileHandler: Processed location redirect for client_fd %d",
            conn->client_fd_);
        return AGAIN;  // Indicates that the request has been redirected
    }

    // Validate request method (must be GET or POST)
    const std::string& request_method = conn->request_data_->method_;
    if (request_method != "GET" && request_method != "POST") {
        log(LOG_ERROR, "CgiHandler: Invalid method '%s' for client_fd %d",
            request_method.c_str(), conn->client_fd_);
        conn->response_data_->set_error_header("Allow", "GET, POST");
        conn->status_ = METHOD_NOT_ALLOWED;
        return ERROR;
    }

    // Resolve the absolute path to the CGI script
    std::string script_path = parse_absolute_path(conn);
    if (script_path.empty()) {
        log(LOG_ERROR,
            "CgiHandler: Failed to determine script path for client_fd %d",
            conn->client_fd_);
        conn->status_ = INTERNAL_SERVER_ERROR;
        return ERROR;
    }

    // Security: Do not allow executing a directory
    if (script_path[script_path.length() - 1] == '/') {
        log(LOG_ERROR, "CgiHandler: Attempt to execute a directory: %s",
            script_path.c_str());
        conn->status_ = FORBIDDEN;
        return ERROR;
    }

    // Check script extension against allowed types
    bool is_valid_extension = false;
    std::string extension;
    size_t dot_pos = script_path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        extension = script_path.substr(dot_pos + 1);
        // This should ideally come from the config file
        if (extension == "php" || extension == "py" || extension == "sh") {
            is_valid_extension = true;
        }
    }
    if (!is_valid_extension) {
        log(LOG_ERROR,
            "CgiHandler: Invalid script extension '%s' for script '%s'",
            extension.c_str(), script_path.c_str());
        conn->status_ = FORBIDDEN;
        return ERROR;
    }

    // Use a single stat() call to check existence, type, and permissions
    struct stat file_stat;
    if (stat(script_path.c_str(), &file_stat) != 0) {
        if (errno == ENOENT) {
            log(LOG_ERROR, "CgiHandler: Script not found: %s",
                script_path.c_str());
            conn->status_ = NOT_FOUND;
        } else if (errno == EACCES) {
            log(LOG_ERROR, "CgiHandler: Access denied to script path: %s",
                script_path.c_str());
            conn->status_ = FORBIDDEN;
        } else {
            log(LOG_ERROR, "CgiHandler: stat() error for script %s: %s",
                script_path.c_str(), strerror(errno));
            conn->status_ = INTERNAL_SERVER_ERROR;
        }
        return ERROR;
    }

    // Check if it's a regular file
    if (!S_ISREG(file_stat.st_mode)) {
        log(LOG_ERROR, "CgiHandler: Script is not a regular file: %s",
            script_path.c_str());
        conn->status_ = FORBIDDEN;
        return ERROR;
    }

    // Check if the script is executable by the user
    if (!(file_stat.st_mode & S_IXUSR)) {
        log(LOG_ERROR, "CgiHandler: Script is not executable: %s",
            script_path.c_str());
        conn->status_ = FORBIDDEN;
        return ERROR;
    }

    // All checks passed. Store the validated path for the setup phase.
    conn->cgi_context_->cgi_script_path_ = script_path;
    log(LOG_DEBUG, "CgiHandler: Permissions check passed for script: %s",
        script_path.c_str());

    return COMPLETE;
}

bool CgiHandler::setup_cgi_pipes(Connection* conn, int server_to_cgi_pipe[2],
                                 int cgi_to_server_pipe[2]) {
    if (pipe(server_to_cgi_pipe) == -1) {
        log(LOG_ERROR, "Pipe server_to_cgi_pipe creation error: %s",
            strerror(errno));
        conn->status_ = INTERNAL_SERVER_ERROR;
        return false;
    }

    if (pipe(cgi_to_server_pipe) == -1) {
        log(LOG_ERROR, "Pipe cgi_to_server_pipe creation error: %s",
            strerror(errno));
        conn->status_ = INTERNAL_SERVER_ERROR;
        return false;
    }

    log(LOG_DEBUG,
        "CGI pipes created: server_to_cgi_pipe: %d and %d, cgi_to_server_pipe: "
        "%d and %d",
        server_to_cgi_pipe[0], server_to_cgi_pipe[1], cgi_to_server_pipe[0],
        cgi_to_server_pipe[1]);
    return true;
}

void CgiHandler::handle_child_pipes(int server_to_cgi_pipe[2],
                                    int cgi_to_server_pipe[2]) {
    // Close the write-end of the pipe to CGI's stdin
    close(server_to_cgi_pipe[1]);
    server_to_cgi_pipe[1] = -1;
    // Close the write-end of the pipe from CGI's stdout
    close(cgi_to_server_pipe[0]);
    cgi_to_server_pipe[0] = -1;

    if (dup2(server_to_cgi_pipe[0], STDIN_FILENO) == -1) {
        log(LOG_ERROR, "Failed to redirect stdin to CGI pipe: %s",
            strerror(errno));
        _exit(EXIT_FAILURE);
    }

    if (dup2(cgi_to_server_pipe[1], STDOUT_FILENO) == -1) {
        log(LOG_ERROR, "Failed to redirect stdout to CGI pipe: %s",
            strerror(errno));
        _exit(EXIT_FAILURE);
    }

    int stderr_fd =
        open("./cgi_errors.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (stderr_fd != -1) {
        if (dup2(stderr_fd, STDERR_FILENO) == -1) {
            close(stderr_fd);
            _exit(EXIT_FAILURE);
        }
        close(stderr_fd);
    }

    close(server_to_cgi_pipe[0]);
    server_to_cgi_pipe[0] = -1;
    close(cgi_to_server_pipe[1]);
    cgi_to_server_pipe[1] = -1;
}

std::vector<char*> CgiHandler::create_cgi_envp(Connection* conn) {
    std::vector<std::string>& cgi_env_strings = conn->cgi_context_->cgi_envp_;
    std::vector<char*> envp_char_array;

    cgi_env_strings.push_back("REQUEST_METHOD=" + conn->request_data_->method_);
    cgi_env_strings.push_back("SCRIPT_NAME=" +
                              conn->cgi_context_->cgi_script_path_);
    cgi_env_strings.push_back("SERVER_PROTOCOL=" +
                              conn->request_data_->version_);
    cgi_env_strings.push_back("SERVER_SOFTWARE=webserv/4.2");

    if (!conn->request_data_->query_string_.empty()) {
        cgi_env_strings.push_back("QUERY_STRING=" +
                                  conn->request_data_->query_string_);
    }

    cgi_env_strings.push_back("SCRIPT_FILENAME=" +
                              conn->cgi_context_->cgi_script_path_);
    cgi_env_strings.push_back("GATEWAY_INTERFACE=CGI/1.1");
    cgi_env_strings.push_back("SERVER_NAME=" + conn->virtual_server_->host_);
    std::ostringstream oss;
    oss << conn->virtual_server_->port_;
    cgi_env_strings.push_back("SERVER_PORT=" + oss.str());

    // Add CONTENT_TYPE, CONTENT_LENGTH for POST
    bool content_type_set = false;
    bool content_length_set = false;
    if (conn->request_data_->method_ == "POST") {
        std::string ct = conn->request_data_->get_header("content-type");
        if (!ct.empty()) {
            cgi_env_strings.push_back("CONTENT_TYPE=" + ct);
            content_type_set = true;
        }
        std::string cl = conn->request_data_->get_header("content-length");
        if (!cl.empty()) {
            cgi_env_strings.push_back("CONTENT_LENGTH=" + cl);
            content_length_set = true;
        }
    }

    // Add all HTTP_ headers
    for (std::map<std::string, std::string>::const_iterator it =
             conn->request_data_->headers_.begin();
         it != conn->request_data_->headers_.end(); ++it) {
        // Skip content-type and content-length if they were already set
        // directly
        if (content_type_set && it->first == "content-type") {
            continue;
        }
        if (content_length_set && it->first == "content-length") {
            continue;
        }

        std::string header_name_env = "HTTP_";
        for (size_t i = 0; i < it->first.length(); ++i) {
            char c = it->first[i];
            header_name_env +=
                (c == '-') ? '_' : std::toupper(static_cast<unsigned char>(c));
        }
        cgi_env_strings.push_back(header_name_env + "=" + it->second);
    }

    for (size_t i = 0; i < cgi_env_strings.size(); ++i) {
        envp_char_array.push_back(
            const_cast<char*>(cgi_env_strings[i].c_str()));
    }
    envp_char_array.push_back(NULL);

    log(LOG_DEBUG, "CGI environment variables created for client %d",
        conn->client_fd_);

    for (std::vector<std::string>::const_iterator it = cgi_env_strings.begin();
         it != cgi_env_strings.end(); ++it) {
        log(LOG_TRACE, "CGI env: %s", it->c_str());
    }

    return envp_char_array;
}

void CgiHandler::execute_cgi_script(Connection* conn, char** envp) {
    char* cgi_script_path_cstr =
        const_cast<char*>(conn->cgi_context_->cgi_script_path_.c_str());

    std::string script_path = conn->cgi_context_->cgi_script_path_;
    size_t last_slash = script_path.find_last_of('/');
    if (last_slash != std::string::npos) {
        std::string script_dir = script_path.substr(0, last_slash);
        if (chdir(script_dir.c_str()) == -1) {
            log(LOG_ERROR,
                "execute_cgi_script: connection '%d' failed to chdir to '%s': "
                "%s",
                conn->client_fd_, script_dir.c_str(), strerror(errno));
            _exit(EXIT_FAILURE);
        }
        log(LOG_DEBUG,
            "execute_cgi_script: connection '%d' changed directory to '%s'",
            conn->client_fd_, script_dir.c_str());
    }

    // For shebang execution, argv[0] is the script path.
    // The OS uses the shebang to find the actual interpreter.
    char* const argv[] = {cgi_script_path_cstr, NULL};

    log(LOG_INFO,
        "execute_cgi_script: connection '%d' attempting to execute '%s'",
        conn->client_fd_, cgi_script_path_cstr);

    if (execve(cgi_script_path_cstr, argv, envp) == -1) {
        log(LOG_ERROR,
            "execute_cgi_script: connection '%d' failed to execute CGI script "
            "'%s': %s",
            conn->client_fd_, cgi_script_path_cstr, strerror(errno));
        _exit(EXIT_FAILURE);
    }
}

bool CgiHandler::handle_parent_pipes(Connection* conn,
                                     int server_to_cgi_pipe[2],
                                     int cgi_to_server_pipe[2]) {
    close(server_to_cgi_pipe[0]);  // Parent closes read-end of pipe
    server_to_cgi_pipe[0] = -1;    // Mark as closed
    conn->cgi_context_->cgi_pipe_stdin_fd_ =
        server_to_cgi_pipe[1];  // Parent keeps write-end

    close(cgi_to_server_pipe[1]);  // Parent closes write-end of pipe
    cgi_to_server_pipe[1] = -1;    // Mark as closed
    conn->cgi_context_->cgi_pipe_stdout_fd_ =
        cgi_to_server_pipe[0];  // Parent keeps read-end

    if (!set_non_blocking(conn->cgi_context_->cgi_pipe_stdin_fd_)) {
        log(LOG_ERROR,
            "Failed to set CGI stdin pipe to non-blocking mode for client %d",
            conn->client_fd_);
        conn->status_ = INTERNAL_SERVER_ERROR;
        return false;
    }

    if (!set_non_blocking(conn->cgi_context_->cgi_pipe_stdout_fd_)) {
        log(LOG_ERROR,
            "Failed to set CGI stdout pipe to non-blocking mode for client %d",
            conn->client_fd_);
        conn->status_ = INTERNAL_SERVER_ERROR;
        return false;
    }

    log(LOG_DEBUG, "Parent pipes set up for CGI: stdin_fd=%d, stdout_fd=%d",
        conn->cgi_context_->cgi_pipe_stdin_fd_,
        conn->cgi_context_->cgi_pipe_stdout_fd_);

    return true;
}

void CgiHandler::cleanup_handler(Connection* conn) {
    if (!conn->cgi_context_) {
        log(LOG_FATAL,
            "CgiHandler: Cleanup called but cgi_context_ is NULL for client_fd "
            "%d",
            conn->client_fd_);
        return;
    }

    // Remove pipe IO contexts from epoll monitoring
    // look in the vector of IO contexts and remove the ones related to CGI
    // pipes if (conn->cgi_context_->cgi_pipe_stdin_fd_) {
    //     conn->remove_io_context(conn->cgi_context_->cgi_pipe_stdin_fd_);
    // }
    // if (conn->cgi_context_->cgi_pipe_stdout_io_ctx_) {
    //     conn->remove_io_context(conn->cgi_context_->cgi_pipe_stdout_io_ctx_);
    // }

    if (conn->cgi_context_->cgi_pid_ > 0) {
        int status;
        pid_t result = waitpid(conn->cgi_context_->cgi_pid_, &status, WNOHANG);

        if (result == 0) {
            log(LOG_INFO,
                "Killing remaining CGI child process %d for client %d",
                conn->cgi_context_->cgi_pid_, conn->client_fd_);
            kill(conn->cgi_context_->cgi_pid_, SIGKILL);
            waitpid(conn->cgi_context_->cgi_pid_, &status, 0);
        }
    }

    if (conn->cgi_context_->cgi_pipe_stdin_fd_ != -1) {
        close(conn->cgi_context_->cgi_pipe_stdin_fd_);
        conn->cgi_context_->cgi_pipe_stdin_fd_ = -1;
    }
    if (conn->cgi_context_->cgi_pipe_stdout_fd_ != -1) {
        close(conn->cgi_context_->cgi_pipe_stdout_fd_);
        conn->cgi_context_->cgi_pipe_stdout_fd_ = -1;
    }

    if (conn->cgi_context_) {
        delete conn->cgi_context_;
        conn->cgi_context_ = NULL;
    }

    log(LOG_DEBUG, "CgiHandler: Cleanup complete for client_fd %d",
        conn->client_fd_);
}

Result CgiHandler::handle_cgi_write(Connection* conn) {
    if (!conn || !conn->cgi_context_ ||
        conn->cgi_context_->cgi_pipe_stdin_fd_ < 0) {
        log(LOG_ERROR, "CGI write: Invalid connection or pipe");
        conn->status_ = INTERNAL_SERVER_ERROR;
        return ERROR;
    }

    Buffer& body_buffer = conn->request_data_->body_buffer_;
    int fd = conn->cgi_context_->cgi_pipe_stdin_fd_;

    ssize_t bytes_written = body_buffer.write_to(fd);

    if (bytes_written < 0) {
        log(LOG_ERROR, "Failed to write to CGI stdin pipe: %s",
            strerror(errno));
        conn->status_ = INTERNAL_SERVER_ERROR;
        return ERROR;
    }

    if (conn->request_data_->body_fully_parsed_ && body_buffer.empty()) {
        close(fd);
        conn->cgi_context_->cgi_pipe_stdin_fd_ = -1;

        IOContext* stdout_ctx = conn->add_io_context(
            conn->cgi_context_->cgi_pipe_stdout_fd_, FD_CGI_PIPE_READ, EPOLLIN);

        if (!stdout_ctx) {
            log(LOG_ERROR,
                "Failed to register CGI stdout pipe with epoll for client %d",
                conn->client_fd_);
            conn->status_ = INTERNAL_SERVER_ERROR;
            return ERROR;
        }

        log(LOG_DEBUG,
            "CGI: Finished writing to stdin, switching to read mode for client "
            "%d",
            conn->client_fd_);
        return COMPLETE;
    } else {
        log(LOG_DEBUG,
            "Partial write to CGI stdin pipe for client %d, %zu bytes left",
            conn->client_fd_, body_buffer.readable_bytes());
        return AGAIN;
    }
}

Result CgiHandler::handle_cgi_read(Connection* conn) {
    log(LOG_DEBUG, "CGI: Handling read for client %d on stdout_fd %d",
        conn->client_fd_, conn->cgi_context_->cgi_pipe_stdout_fd_);

    if (conn->cgi_context_->cgi_pipe_stdout_fd_ < 0) {
        log(LOG_FATAL,
            "CGI: Attempt to read from invalid pipe_stdout_fd for client %d.",
            conn->client_fd_);
        conn->status_ = INTERNAL_SERVER_ERROR;
        return ERROR;
    }

    Buffer& buffer = conn->cgi_context_->cgi_output_buffer_;
    ssize_t bytes_read =
        buffer.read_from(conn->cgi_context_->cgi_pipe_stdout_fd_);
    if (bytes_read < 0) {
        log(LOG_ERROR, "CGI: Failed to read from stdout pipe for client %d: %s",
            conn->client_fd_, strerror(errno));
        conn->status_ = BAD_GATEWAY;
        return ERROR;
    }

    if (bytes_read > 0) {
        log(LOG_DEBUG,
            "CGI: Read %zd bytes from stdout for client %d. Total buffer: %zu",
            bytes_read, conn->client_fd_, buffer.readable_bytes());
        Result result = parse_cgi_output(conn);
        if (result == ERROR) {
            log(LOG_ERROR,
                "CGI: Error state reached for client %d, cleaning up resources",
                conn->client_fd_);
            return ERROR;
        }
        if (result == AGAIN) {
            log(LOG_DEBUG, "CGI: Need more data for client %d",
                conn->client_fd_);
            return AGAIN;  // Haven't found end of headers yet
        }

        if (!set_status_line(conn)) {
            log(LOG_ERROR, "CGI: Failed to set status line for client %d",
                conn->client_fd_);
            conn->status_ = INTERNAL_SERVER_ERROR;
            return ERROR;
        }

        set_cgi_body_handling(conn);
    }

    log(LOG_DEBUG, "CGI: EOF received from stdout for client %d.",
        conn->client_fd_);

    // Headers not fully processed before EOF
    // CHECK if this is the right condition to check
    if (buffer.readable_bytes() == 0 &&
        conn->response_data_->headers_.empty()) {
        // No data received - script execution failure
        log(LOG_WARNING, "CGI: No output received from script for client %d",
            conn->client_fd_);
        // CHECK: if we should return 500 or 502
        conn->status_ = BAD_GATEWAY;
        return ERROR;
    }

    return COMPLETE;
}

Result CgiHandler::parse_cgi_output(Connection* conn) {
    log(LOG_DEBUG, "Parsing cgi output for connection: %i", conn->client_fd_);

    enum CgiParseState {
        CGI_LINE_START,
        CGI_NAME,
        CGI_SPACE_BEFORE_VALUE,
        CGI_VALUE,
        CGI_VALUE_ALMOST_DONE,
        CGI_HEADERS_ALMOST_DONE,
    };

    Buffer& buff = conn->cgi_context_->cgi_output_buffer_;
    CgiContext* context = conn->cgi_context_;
    unsigned int& state = context->state_;

    while (buff.readable_bytes() > 0) {
        const char ch = buff.peek();

        switch (state) {
            case CGI_LINE_START:
                if (is_line_ending_char(ch)) {
                    state = CGI_HEADERS_ALMOST_DONE;
                    // Don't consume yet - let HEADERS_ALMOST_DONE handle it
                    continue;
                }

                if (!is_token_char(ch)) {
                    return ERROR;
                }

                context->key_start_ = buff.data();
                state = CGI_NAME;
                // Skip the buff.consume(1) at the end of the loop
                continue;

            case CGI_NAME:
                if (ch == ':') {
                    if (buff.data() == context->key_start_) {
                        return ERROR;
                    }
                    context->key_end_ = buff.data();
                    state = CGI_SPACE_BEFORE_VALUE;
                } else if (!is_token_char(ch)) {
                    return ERROR;
                } else if (static_cast<size_t>(buff.data() -
                                               context->key_start_) >=
                           http_limits::MAX_HEADER_NAME_LENGTH) {
                    return ERROR;
                }
                break;

            case CGI_SPACE_BEFORE_VALUE:
                if (ch == ' ' || ch == '\t') {
                    break;
                }

                if (is_line_ending_char(ch)) {
                    context->value_start_ = NULL;
                    context->value_end_ = NULL;
                    state = CGI_VALUE_ALMOST_DONE;
                    // Don't consume yet - let VALUE_ALMOST_DONE handle it
                    continue;
                }

                context->value_start_ = buff.data();
                state = CGI_VALUE;
                continue;  // Re-evaluate this character in the new VALUE state.

            case CGI_VALUE:
                if (is_line_ending_char(ch)) {
                    context->value_end_ = buff.data();
                    state = CGI_VALUE_ALMOST_DONE;
                    // Don't consume yet - let VALUE_ALMOST_DONE handle it
                    continue;
                }

                // A header value can contain any visible ASCII character,
                // spaces, and horizontal tabs. It MUST NOT contain other
                // control characters. (RFC 7230, Section 3.2)
                if (iscntrl(ch) && ch != '\t') {
                    return ERROR;
                }

                if (static_cast<size_t>(buff.data() - context->value_start_) >
                    http_limits::MAX_HEADER_VALUE_LENGTH) {
                    return ERROR;
                }
                break;

            case CGI_VALUE_ALMOST_DONE:
                if (ch == '\r') {
                    // Skip \r, expect \n next (or treat as line ending if
                    // standalone)
                    break;
                } else if (ch == '\n') {
                    commit_cgi_header(conn->response_data_, context);
                    context->key_start_ = NULL;
                    context->key_end_ = NULL;
                    context->value_start_ = NULL;
                    context->value_end_ = NULL;
                    state = CGI_LINE_START;
                } else {
                    return ERROR;
                }
                break;

            case CGI_HEADERS_ALMOST_DONE:
                if (ch == '\r') {
                    // Skip \r, expect \n next (or treat as end if standalone)
                    break;
                } else if (ch == '\n') {
                    buff.consume(1);
                    return COMPLETE;
                } else {
                    return ERROR;
                }
        }

        // Consume the character and move to the next one in the buffer
        buff.consume(1);
    }
    // If we exit the loop, it's because the buffer is empty. We need more
    // data.
    return AGAIN;
}

void CgiHandler::commit_cgi_header(HttpResponse* response,
                                   const CgiContext* context) {
    std::string header_name(context->key_start_,
                            context->key_end_ - context->key_start_);

    std::string header_value;
    if (context->value_start_ && context->value_end_) {
        header_value.assign(context->value_start_,
                            context->value_end_ - context->value_start_);
    }

    response->set_header(header_name, header_value);
}

bool CgiHandler::set_status_line(Connection* conn) {
    log(LOG_DEBUG, "Setting status line for client %d", conn->client_fd_);

    std::string status_str = conn->response_data_->get_header("status");
    if (!status_str.empty()) {
        // Handle both "200" and "200 OK" formats
        size_t space_pos = status_str.find(' ');
        std::string status_code_str = (space_pos != std::string::npos)
                                          ? status_str.substr(0, space_pos)
                                          : status_str;

        char* end_ptr;
        size_t status = std::strtoul(status_code_str.c_str(), &end_ptr, 10);
        if (*end_ptr != '\0' || status == 0 || status > 599) {
            log(LOG_ERROR, "Invalid Status header value '%s' for client %d",
                status_str.c_str(), conn->client_fd_);
            return false;
        }
        conn->response_data_->status_code_ = static_cast<int>(status);
    } else {
        conn->response_data_->status_code_ = OK;
    }

    conn->response_data_->status_message_ =
        get_status_message(conn->response_data_->status_code_);

    conn->response_data_->version_ = conn->request_data_->version_;

    return true;
}

void CgiHandler::set_cgi_body_handling(Connection* conn) {
    log(LOG_DEBUG, "Determining CGI body handling for client %d",
        conn->client_fd_);

    Buffer& buffer = conn->cgi_context_->cgi_output_buffer_;

    // Check if there's remaining data in the buffer after headers
    if (buffer.readable_bytes() > 0) {
        log(LOG_DEBUG,
            "CGI: Found %zu bytes of body data in buffer for client %d",
            buffer.readable_bytes(), conn->client_fd_);

        // Transfer remaining buffer data to response body
        conn->response_data_->body_data_.assign(
            buffer.data(), buffer.data() + buffer.readable_bytes());
        buffer.consume(buffer.readable_bytes());

        log(LOG_DEBUG,
            "CGI: Transferred %zu bytes from output buffer to response body "
            "for client %d",
            conn->response_data_->body_data_.size(), conn->client_fd_);
    }

    conn->response_data_->body_fd_ = conn->cgi_context_->cgi_pipe_stdout_fd_;

    log(LOG_DEBUG, "CGI: Body handling setup complete for client %d",
        conn->client_fd_);
}
