#include "webserv.hpp"

CgiHandler::CgiHandler() {}

CgiHandler::~CgiHandler() {}

void CgiHandler::handle(Connection* conn) {
    log(LOG_DEBUG, "CgiHandler: Starting processing for client_fd %d",
        conn->client_fd_);

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
        case codes::CGI_HANDLER_HEADERS_PARSED:
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
        conn->response_data_->set_header("Allow", "GET, POST");
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
    conn->cgi_script_path_ = parse_absolute_path(conn);
    if (conn->cgi_script_path_.empty()) {
        log(LOG_ERROR, "Failed to determine CGI script path for URI: %s",
            request_uri.c_str());
        ErrorHandler::generate_error_response(conn,
                                              codes::INTERNAL_SERVER_ERROR);
        return false;
    }

    // 5. Script Extension Check
    bool is_valid_extension = false;
    std::string extension;
    size_t dot_pos = conn->cgi_script_path_.find_last_of('.');
    if (dot_pos != std::string::npos) {
        extension = conn->cgi_script_path_.substr(dot_pos + 1);
        // --CHECK - Check against allowed extensions
        if (extension == "php" || extension == "py" || extension == "sh") {
            is_valid_extension = true;
        }
    }

    if (!is_valid_extension) {
        log(LOG_ERROR, "Invalid CGI script extension '%s' for script '%s'",
            extension.c_str(), conn->cgi_script_path_.c_str());
        ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
        return false;
    }

    // 6. Script Existence Check
    struct stat file_info;
    if (stat(conn->cgi_script_path_.c_str(), &file_info) == -1) {
        if (errno == ENOENT) {
            log(LOG_ERROR, "CGI script '%s' not found",
                conn->cgi_script_path_.c_str());
            ErrorHandler::generate_error_response(conn, codes::NOT_FOUND);
        } else {
            log(LOG_ERROR, "Failed to access CGI script '%s': %s",
                conn->cgi_script_path_.c_str(), strerror(errno));
            ErrorHandler::generate_error_response(conn,
                                                  codes::INTERNAL_SERVER_ERROR);
        }
        return false;
    }

    // 7. Script Type Validation
    if (!S_ISREG(file_info.st_mode)) {
        log(LOG_ERROR, "CGI script '%s' is not a regular file",
            conn->cgi_script_path_.c_str());
        ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
        return false;
    }

    // 8. Script Permission Check
    if (!(file_info.st_mode & S_IXUSR)) {  // Check for user execute permission
        log(LOG_ERROR, "CGI script '%s' is not executable",
            conn->cgi_script_path_.c_str());
        ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
        return false;
    }

    log(LOG_DEBUG, "CGI request validated for script: %s",
        conn->cgi_script_path_.c_str());
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
        handle_child_pipes(server_to_cgi_pipe, cgi_to_server_pipe);
        std::vector<char*> envp = create_cgi_envp(conn);
        execute_cgi_script(conn, envp.data());
    } else {
        // This code runs ONLY in the PARENT process
        conn->cgi_pid_ = pid;
        if (!handle_parent_pipes(conn, server_to_cgi_pipe,
                                 cgi_to_server_pipe)) {
            // ErrorHandler::generate_error_response was called in
            // handle_parent_pipes
            return false;
        }

        if (request_method == "POST" && !conn->request_data_->body_.empty()) {
            conn->cgi_handler_state_ = codes::CGI_HANDLER_WRITING_TO_PIPE;

            // Register this pipe with epoll for EPOLLOUT events
            if (!WebServer::register_epoll_events(conn->cgi_pipe_stdin_fd_,
                                                  EPOLLOUT)) {
                log(LOG_ERROR, "Failed to register CGI stdin pipe with epoll");
                finalize_cgi_error(conn, codes::INTERNAL_SERVER_ERROR);
                return false;
            }

            log(LOG_DEBUG,
                "CGI: POST request, state -> WRITING_TO_PIPE for client %d, "
                "stdin_fd %d",
                conn->client_fd_, conn->cgi_pipe_stdin_fd_);
        } else {
            conn->cgi_handler_state_ = codes::CGI_HANDLER_READING_FROM_PIPE;

            // If it's a GET request or an empty POST, we can close the stdin
            // pipe
            if (conn->cgi_pipe_stdin_fd_ != -1) {
                WebServer::unregister_active_pipe(conn->cgi_pipe_stdin_fd_);
                close(conn->cgi_pipe_stdin_fd_);
                conn->cgi_pipe_stdin_fd_ = -1;  // Mark as closed
                log(LOG_DEBUG,
                    "CGI: Closed stdin pipe immediately for "
                    "non-POST/empty-POST for client %d",
                    conn->client_fd_);
            }

            // Register this pipe with epoll for EPOLLIN events
            if (!WebServer::register_epoll_events(conn->cgi_pipe_stdout_fd_,
                                                  EPOLLIN)) {
                log(LOG_ERROR, "Failed to register CGI stdout pipe with epoll");
                finalize_cgi_error(conn, codes::INTERNAL_SERVER_ERROR);
                return false;
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

void CgiHandler::handle_child_pipes(int server_to_cgi_pipe[2],
                                    int cgi_to_server_pipe[2]) {
    // Close the read-end of the pipe to CGI's stdin
    close(server_to_cgi_pipe[1]);
    // Close the write-end of the pipe from CGI's stdout
    close(cgi_to_server_pipe[0]);

    // Redirect stdin to the read-end of the server_to_cgi_pipe
    if (dup2(server_to_cgi_pipe[0], STDIN_FILENO) == -1) {
        log(LOG_ERROR, "Failed to redirect stdin to CGI pipe: %s",
            strerror(errno));
        _exit(EXIT_FAILURE);
    }

    // Redirect stdout to the write-end of the cgi_to_server_pipe
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

    // Close the original pipe file descriptors in the child process
    close(server_to_cgi_pipe[0]);
    close(cgi_to_server_pipe[1]);
}

std::vector<char*> CgiHandler::create_cgi_envp(Connection* conn) {
    std::vector<std::string>& cgi_env_strings =
        conn->cgi_envp_;                 // To store "NAME=VALUE"
    std::vector<char*> envp_char_array;  // To store char* for execve

    cgi_env_strings.push_back("REQUEST_METHOD=" + conn->request_data_->method_);
    cgi_env_strings.push_back("SCRIPT_NAME=" + conn->cgi_script_path_);
    cgi_env_strings.push_back("SERVER_PROTOCOL=" +
                              conn->request_data_->version_);
    cgi_env_strings.push_back("SERVER_SOFTWARE=webserv/1.0");

    if (!conn->request_data_->query_string_.empty()) {
        cgi_env_strings.push_back("QUERY_STRING=" +
                                  conn->request_data_->query_string_);
    }

    cgi_env_strings.push_back("SCRIPT_FILENAME=" + conn->cgi_script_path_);
    cgi_env_strings.push_back("GATEWAY_INTERFACE=CGI/1.1");
    cgi_env_strings.push_back("SERVER_NAME=" +
                              conn->virtual_server_->host_name_);
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

    // Convert vector of std::string to vector of char*
    for (size_t i = 0; i < cgi_env_strings.size(); ++i) {
        envp_char_array.push_back(
            const_cast<char*>(cgi_env_strings[i].c_str()));
    }
    envp_char_array.push_back(NULL);  // Null-terminate

    log(LOG_DEBUG, "CGI environment variables created for client %d",
        conn->client_fd_);

    // Log the environment variables for debugging
    for (std::vector<std::string>::const_iterator it = cgi_env_strings.begin();
         it != cgi_env_strings.end(); ++it) {
        log(LOG_TRACE, "CGI env: %s", it->c_str());
    }

    return envp_char_array;
}

void CgiHandler::execute_cgi_script(Connection* conn, char** envp) {
    char* cgi_script_path_cstr =
        const_cast<char*>(conn->cgi_script_path_.c_str());

    // For shebang execution, argv[0] is the script path.
    // The OS uses the shebang to find the actual interpreter.
    char* const argv[] = {cgi_script_path_cstr, NULL};

    log(LOG_INFO,
        "execute_cgi_script: connection '%d' attempting to execute '%s'",
        conn->client_fd_, cgi_script_path_cstr);

    // Execute the CGI script
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
    conn->cgi_pipe_stdin_fd_ = server_to_cgi_pipe[1];  // Parent keeps write-end

    close(cgi_to_server_pipe[1]);  // Parent closes write-end of pipe
    conn->cgi_pipe_stdout_fd_ = cgi_to_server_pipe[0];  // Parent keeps read-end

    // Set the pipe stdin to non-blocking mode
    if (!WebServer::set_non_blocking(conn->cgi_pipe_stdin_fd_)) {
        log(LOG_ERROR,
            "Failed to set CGI stdin pipe to non-blocking mode for "
            "client %d",
            conn->client_fd_);
        finalize_cgi_error(conn, codes::INTERNAL_SERVER_ERROR);
        return false;
    }

    // Set the pipe stdout to non-blocking mode
    if (!WebServer::set_non_blocking(conn->cgi_pipe_stdout_fd_)) {
        log(LOG_ERROR,
            "Failed to set CGI stdout pipe to non-blocking mode for "
            "client %d",
            conn->client_fd_);
        finalize_cgi_error(conn, codes::INTERNAL_SERVER_ERROR);
        return false;
    }

    // Register the pipe with the ConnectionManager
    WebServer::register_active_pipe(conn->cgi_pipe_stdin_fd_, conn);
    WebServer::register_active_pipe(conn->cgi_pipe_stdout_fd_, conn);

    log(LOG_DEBUG, "Parent pipes set up for CGI: stdin_fd=%d, stdout_fd=%d",
        conn->cgi_pipe_stdin_fd_, conn->cgi_pipe_stdout_fd_);
    return true;
}

void CgiHandler::handle_cgi_write(Connection* conn) {
    // Write request body to CGI's stdin pipe
    ssize_t bytes_written =
        write(conn->cgi_pipe_stdin_fd_, conn->request_data_->body_.data(),
              conn->request_data_->body_.size());

    if (bytes_written < 0) {
        log(LOG_ERROR, "Failed to write to CGI stdin pipe: %s",
            strerror(errno));
        finalize_cgi_error(conn, codes::INTERNAL_SERVER_ERROR);
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
        if (!WebServer::register_epoll_events(conn->cgi_pipe_stdout_fd_,
                                              EPOLLIN)) {
            log(LOG_ERROR, "Failed to register CGI stdout pipe with epoll");
            finalize_cgi_error(conn, codes::INTERNAL_SERVER_ERROR);
            return;
        }

    } else {
        // Still have more data to write, keep the state as writing
        log(LOG_DEBUG, "Partial write to CGI stdin pipe for client %d",
            conn->client_fd_);
    }
}

void CgiHandler::handle_cgi_read(Connection* conn) {
    log(LOG_DEBUG,
        "CGI: Handling read for client %d on stdout_fd %d, current cgi_state: "
        "%d",
        conn->client_fd_, conn->cgi_pipe_stdout_fd_, conn->cgi_handler_state_);

    if (conn->cgi_pipe_stdout_fd_ < 0) {
        log(LOG_FATAL,
            "CGI: Attempt to read from invalid pipe_stdout_fd for client %d.",
            conn->client_fd_);
        finalize_cgi_error(conn, codes::INTERNAL_SERVER_ERROR);
        return;
    }

    if (conn->cgi_pid_ > 0) {
        int status;
        pid_t result = waitpid(conn->cgi_pid_, &status, WNOHANG);

        if (result == 0) {
            // Child process is still running - just return and wait
            log(LOG_TRACE, "CGI process %d still running for client %d",
                conn->cgi_pid_, conn->client_fd_);
            return;  // Exit early, epoll will call us again
        } else if (result > 0) {
            // Child has terminated - reap it and continue
            log(LOG_INFO, "CGI process %d terminated for client %d",
                conn->cgi_pid_, conn->client_fd_);
            conn->cgi_pid_ = -1;  // Mark as reaped
            // Continue to read remaining output below
        }
        // result < 0 means error, but continue anyway (process might be gone)
    }

    // Resize the read buffer to accommodate incoming data
    size_t original_size = conn->cgi_read_buffer_.size();
    conn->cgi_read_buffer_.resize(original_size + CHUNK_SIZE);

    // Read from CGI's stdout pipe
    ssize_t bytes_read =
        read(conn->cgi_pipe_stdout_fd_, &conn->cgi_read_buffer_[original_size],
             CHUNK_SIZE);

    if (bytes_read < 0) {
        log(LOG_ERROR, "CGI: Failed to read from stdout pipe for client %d: %s",
            conn->client_fd_, strerror(errno));
        finalize_cgi_error(conn, codes::BAD_GATEWAY);
        return;
    }

    // Resize the buffer to the actual size read
    conn->cgi_read_buffer_.resize(original_size + bytes_read);

    if (bytes_read > 0) {
        log(LOG_DEBUG,
            "CGI: Read %zd bytes from stdout for client %d. Total buffer: %zu",
            bytes_read, conn->client_fd_, conn->cgi_read_buffer_.size());
        parse_cgi_output(conn);  // This might change conn->cgi_handler_state_
    }

    if (conn->cgi_handler_state_ == codes::CGI_HANDLER_ERROR) {
        log(LOG_ERROR,
            "CGI: Error state reached for client %d, cleaning up resources",
            conn->client_fd_);
        return;
    }

    if (bytes_read == 0) {
        log(LOG_DEBUG, "CGI: EOF received from stdout for client %d.",
            conn->client_fd_);

        if (conn->cgi_handler_state_ == codes::CGI_HANDLER_READING_FROM_PIPE) {
            // Headers not fully parsed before EOF - this is an error
            if (conn->cgi_read_buffer_.empty() &&
                conn->response_data_->headers_.empty()) {
                // No data at all - script execution failure
                log(LOG_WARNING,
                    "CGI: No output received from script for client %d",
                    conn->client_fd_);
                finalize_cgi_error(conn, codes::INTERNAL_SERVER_ERROR);
            } else {
                // Partial data - malformed response
                log(LOG_WARNING,
                    "CGI: Incomplete headers received for client %d",
                    conn->client_fd_);
                finalize_cgi_error(conn, codes::BAD_GATEWAY);
            }
            return;
        }

        // Headers already parsed - check Content-Length if present
        std::string content_length_str =
            conn->response_data_->get_header("content-length");
        if (!content_length_str.empty()) {
            char* end_ptr;
            size_t expected_content_length =
                std::strtoul(content_length_str.c_str(), &end_ptr, 10);
            if (expected_content_length != conn->response_data_->body_.size()) {
                log(LOG_ERROR,
                    "CGI: Content-Length mismatch for client %d. Expected %zu, "
                    "got %zu",
                    conn->client_fd_, expected_content_length,
                    conn->response_data_->body_.size());
                finalize_cgi_error(conn, codes::BAD_GATEWAY);
                return;
            }
        }

        // All good - finalize the response
        finalize_cgi_response(conn);
        return;
    }
}

void CgiHandler::parse_cgi_output(Connection* conn) {
    log(LOG_DEBUG,
        "CGI: Parsing output buffer (size %zu) for client %d, current state: "
        "%d",
        conn->cgi_read_buffer_.size(), conn->client_fd_,
        conn->cgi_handler_state_);

    // 1. Parse Headers
    std::vector<char>& buffer = conn->cgi_read_buffer_;
    while ((conn->cgi_handler_state_ != codes::CGI_HANDLER_HEADERS_PARSED) &&
           (!buffer.empty())) {
        std::vector<char>::iterator line_end_it =
            std::search(buffer.begin(), buffer.end(), CRLF, &CRLF[2]);

        if (line_end_it == buffer.end()) {
            // No complete header line found in current buffer
            log(LOG_DEBUG,
                "CGI: Incomplete header line for client %d. Waiting for more "
                "data.",
                conn->client_fd_);
            return;  // Need more data
        }

        if (line_end_it == buffer.begin()) {  // Empty line (CRLFCRLF)
            log(LOG_DEBUG, "CGI: End of headers found for client %d.",
                conn->client_fd_);
            buffer.erase(buffer.begin(), buffer.begin() + 2);  // Consume CRLF
            conn->cgi_handler_state_ = codes::CGI_HANDLER_HEADERS_PARSED;
            break;  // Move to body parsing
        }

        std::string header_line(buffer.begin(), line_end_it);
        log(LOG_TRACE, "CGI header line: %s", header_line.c_str());

        // Process the header line
        size_t colon_pos = header_line.find(':');
        if (colon_pos == std::string::npos || colon_pos == 0) {
            log(LOG_ERROR, "CGI: Invalid header line for client %d: '%s'",
                conn->client_fd_, header_line.c_str());
            finalize_cgi_error(
                conn, codes::BAD_GATEWAY);  // Malformed response from CGI
            return;
        }

        std::string header_name = header_line.substr(0, colon_pos);
        // Check for invalid characters in key and convert to lowercase
        for (size_t i = 0; i < header_name.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(header_name[i]);

            // RFC 7230: field-name = token
            // token = 1*tchar
            // tchar = "!" / "#" / "$" / "%" / "&" / "'" / "*" / "+" / "-" /
            // "."
            // /
            //         "^" / "_" / "`" / "|" / "~" / DIGIT / ALPHA
            if (!(isalnum(c) || strchr("!#$%&'*+-.^_`|~", c))) {
                log(LOG_ERROR, "Invalid character '%c' in CGI header name: %s",
                    static_cast<char>(c), header_name.c_str());
                finalize_cgi_error(conn, codes::BAD_GATEWAY);
                return;
            }

            header_name[i] = std::tolower(c);
        }

        std::string header_value = header_line.substr(colon_pos + 1);
        size_t value_start = header_value.find_first_not_of(" \t");
        if (value_start != std::string::npos) {
            header_value = header_value.substr(value_start);
        }

        // Store the header in response_data
        conn->response_data_->set_header(header_name, header_value);
        buffer.erase(buffer.begin(),
                     line_end_it + 2);  // Remove the line from the buffer
    }

    if (conn->cgi_handler_state_ == codes::CGI_HANDLER_READING_FROM_PIPE) {
        log(LOG_DEBUG,
            "CGI headers not fully parsed yet, waiting for more data");
        return;
    }

    // 2. Parse Body
    std::string content_length_str =
        conn->response_data_->get_header("content-length");
    if (!content_length_str.empty()) {
        // Content-Length header is present, read the body
        char* end_ptr;
        size_t content_length =
            std::strtoul(content_length_str.c_str(), &end_ptr, 10);
        if (*end_ptr != '\0' || content_length == 0) {
            log(LOG_ERROR,
                "Invalid Content-Length header value '%s' for client %d",
                content_length_str.c_str(), conn->client_fd_);
            finalize_cgi_error(conn, codes::BAD_GATEWAY);
            return;
        }
        if (content_length > buffer.size()) {
            log(LOG_DEBUG,
                "Waiting for more CGI output data, expected %zu bytes, got "
                "%zu",
                content_length, buffer.size());
            return;  // Not enough data for body, wait for more
        }

        // We have enough data for the body
        conn->response_data_->body_.insert(conn->response_data_->body_.end(),
                                           buffer.begin(),
                                           buffer.begin() + content_length);
        buffer.erase(
            buffer.begin(),
            buffer.begin() + content_length);  // Remove body from buffer
        log(LOG_DEBUG, "CGI body read successfully, size: %zu bytes",
            conn->response_data_->body_.size());
        finalize_cgi_response(conn);
    } else {
        // No Content-Length header, append the rest of the buffer as body
        log(LOG_DEBUG,
            "No Content-Length header found in CGI response for client %d. "
            "Appending rest of buffer as body.",
            conn->client_fd_);
        conn->response_data_->body_.insert(conn->response_data_->body_.end(),
                                           buffer.begin(), buffer.end());
        buffer.clear();
    }
}

void CgiHandler::finalize_cgi_response(Connection* conn) {
    if (!set_status_line(conn)) {
        finalize_cgi_error(conn, codes::BAD_GATEWAY);
        return;
    }

    // Set the response headers
    std::ostringstream oss;
    oss << conn->response_data_->body_.size();
    conn->response_data_->set_header("Content-Length", oss.str());

    // Change states
    conn->cgi_handler_state_ = codes::CGI_HANDLER_COMPLETE;
    conn->conn_state_ = codes::CONN_WRITING;

    cleanup_cgi_resources(conn);

    log(LOG_DEBUG, "CGI response finalized for client %d, status: %d",
        conn->client_fd_, conn->response_data_->status_code_);
}

void CgiHandler::finalize_cgi_error(Connection* conn,
                                    codes::ResponseStatus status) {
    ErrorHandler::generate_error_response(conn, status);
    conn->cgi_handler_state_ = codes::CGI_HANDLER_ERROR;

    cleanup_cgi_resources(conn);
}

bool CgiHandler::set_status_line(Connection* conn) {
    std::string status_str = conn->response_data_->get_header("status");
    if (!status_str.empty()) {
        // If status header is present, use it
        char* end_ptr;
        size_t status = std::strtoul(status_str.c_str(), &end_ptr, 10);
        if (*end_ptr != '\0' || status == 0) {
            log(LOG_ERROR, "Invalid Status header value '%s' for client %d",
                status_str.c_str(), conn->client_fd_);
            return false;
        }
        conn->response_data_->status_code_ = static_cast<int>(status);
    } else {
        // Default to 200 OK if no status header is provided
        conn->response_data_->status_code_ = codes::OK;
    }

    conn->response_data_->status_message_ =
        get_status_message(conn->response_data_->status_code_);

    conn->response_data_->version_ = conn->request_data_->version_;

    return true;
}

void CgiHandler::cleanup_cgi_resources(Connection* conn) {
    // Always try to reap/kill any remaining child process
    if (conn->cgi_pid_ > 0) {
        int status;
        pid_t result = waitpid(conn->cgi_pid_, &status, WNOHANG);

        if (result == 0) {
            // Child is still running - kill it
            log(LOG_INFO,
                "Killing remaining CGI child process %d for client %d",
                conn->cgi_pid_, conn->client_fd_);
            kill(conn->cgi_pid_, SIGKILL);
            // Wait for it to die after SIGKILL
            waitpid(conn->cgi_pid_, &status, 0);  // Blocking wait after SIGKILL
        } else if (result > 0) {
            log(LOG_DEBUG,
                "CGI child process %d already terminated for client %d",
                conn->cgi_pid_, conn->client_fd_);
        }
        // result < 0 means error (process doesn't exist), which is fine

        conn->cgi_pid_ = -1;  // Reset PID in all cases
    }

    // Clean up pipes
    if (conn->cgi_pipe_stdin_fd_ != -1) {
        WebServer::unregister_active_pipe(conn->cgi_pipe_stdin_fd_);
        close(conn->cgi_pipe_stdin_fd_);
        conn->cgi_pipe_stdin_fd_ = -1;  // Mark as closed
    }

    if (conn->cgi_pipe_stdout_fd_ != -1) {
        WebServer::unregister_active_pipe(conn->cgi_pipe_stdout_fd_);
        close(conn->cgi_pipe_stdout_fd_);
        conn->cgi_pipe_stdout_fd_ = -1;  // Mark as closed
    }

    // Clear the CGI read buffer
    conn->cgi_read_buffer_.clear();
}
