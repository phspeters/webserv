#include "common.hpp"

StaticFileHandler::StaticFileHandler() {}

StaticFileHandler::~StaticFileHandler() {}

// 1. Initial, lightweight validation of the request.
ResponseStatus StaticFileHandler::check_permissions(Connection* conn) {
    log(LOG_DEBUG, "StaticFileHandler: Checking permissions for client_fd %d",
        conn->client_fd_);

    // Only GET method is supported for static files
    if (conn->request_data_->method_ != "GET") {
        //ErrorHandler::generate_error_response(conn, METHOD_NOT_ALLOWED);
        conn->response_data_->set_error_header("Allow", "GET");
        //conn->conn_state_ = CONN_WRITING_RESPONSE;
        return METHOD_NOT_ALLOWED;
    }

    // Check for location-level redirects first
    if (process_location_redirect(conn)) {
        conn->conn_state_ = CONN_WRITING_RESPONSE;
        return;
    }

    log(LOG_DEBUG,
        "StaticFileHandler: Permissions check passed for client_fd %d",
        conn->client_fd_);
}

// 2. Setup resources: resolve path, open file, and prepare for serving.
ResponseStatus StaticFileHandler::setup_handler(Connection* conn) {
    log(LOG_DEBUG, "StaticFileHandler: Setting up handler for client_fd %d",
        conn->client_fd_);

    // Resolve the absolute path from the request URI
    std::string absolute_path = parse_absolute_path(conn);

    // Handle directory-related logic (redirects, index files, autoindex)
    if (absolute_path.empty() ||
        absolute_path[absolute_path.length() - 1] == '/') {
        // Redirect if URI is a directory but lacks a trailing slash
        if (process_directory_redirect(conn, absolute_path)) {
            conn->conn_state_ = CONN_WRITING_RESPONSE;
            return;
        }

        // Check for index files or generate autoindex listing
        bool need_autoindex = false;
        if (process_directory_index(conn, absolute_path, need_autoindex)) {
            if (need_autoindex) {
                generate_directory_listing(conn, absolute_path);
                conn->conn_state_ = CONN_WRITING_RESPONSE;
                return;
            }
        } else {
            // Error (e.g., 403 Forbidden if no index and autoindex is off)
            conn->conn_state_ = CONN_WRITING_RESPONSE;
            return;
        }
    }

    // At this point, we should have a path to a specific file
    // Open the file and get its metadata
    int fd = open(absolute_path.c_str(), O_RDONLY);
    if (fd == -1) {
        if (errno == ENOENT) {
            ErrorHandler::generate_error_response(conn, NOT_FOUND);
        } else if (errno == EACCES) {
            ErrorHandler::generate_error_response(conn, FORBIDDEN);
        } else {
            ErrorHandler::generate_error_response(conn, INTERNAL_SERVER_ERROR);
        }
        conn->conn_state_ = CONN_WRITING_RESPONSE;
        return;
    }

    // Get file info (size, type)
    struct stat file_info;
    if (fstat(fd, &file_info) == -1) {
        close(fd);
        ErrorHandler::generate_error_response(conn, INTERNAL_SERVER_ERROR);
        conn->conn_state_ = CONN_WRITING_RESPONSE;
        return;
    }

    // Ensure it's a regular file
    if (!S_ISREG(file_info.st_mode)) {
        close(fd);
        ErrorHandler::generate_error_response(conn, FORBIDDEN);
        conn->conn_state_ = CONN_WRITING_RESPONSE;
        return;
    }

    // Create and populate the context for this connection
    conn->static_file_context_ = new StaticFileContext();
    conn->static_file_context_->file_fd_ = fd;
    conn->static_file_context_->bytes_to_send_ = file_info.st_size;

    log(LOG_INFO,
        "StaticFileHandler: Setup complete for client_fd %d, file_fd %d",
        conn->client_fd_, fd);
}

// 3. Main logic: read the file and prepare the response.
ResponseStatus StaticFileHandler::handle_event(Connection* conn) {
    log(LOG_DEBUG, "StaticFileHandler: Handling event for client_fd %d",
        conn->client_fd_);

    if (!conn->static_file_context_ ||
        conn->static_file_context_->file_fd_ < 0) {
        ErrorHandler::generate_error_response(conn, INTERNAL_SERVER_ERROR);
        conn->conn_state_ = CONN_WRITING_RESPONSE;
        return;
    }

    int fd = conn->static_file_context_->file_fd_;
    size_t file_size = conn->static_file_context_->bytes_to_send_;

    // Read the entire file content into a buffer
    //std::vector<char> file_content(file_size);
    //ssize_t bytes_read = read(fd, &file_content[0], file_size);

    //if (bytes_read < 0 || static_cast<size_t>(bytes_read) != file_size) {
    //    ErrorHandler::generate_error_response(conn, INTERNAL_SERVER_ERROR);
    //    conn->conn_state_ = CONN_WRITING_RESPONSE;
    //    return;
    //}

    // Determine content type from file extension
    std::string content_type = "application/octet-stream";
    std::string path = conn->request_data_->path_;
    size_t dot_pos = path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        std::string ext = path.substr(dot_pos + 1);
        if (ext == "html" || ext == "htm")
            content_type = "text/html";
        else if (ext == "css")
            content_type = "text/css";
        else if (ext == "js")
            content_type = "application/javascript";
        else if (ext == "png")
            content_type = "image/png";
        else if (ext == "jpg" || ext == "jpeg")
            content_type = "image/jpeg";
        else if (ext == "gif")
            content_type = "image/gif";
        else if (ext == "txt")
            content_type = "text/plain";
    }

    // Prepare the successful response
    conn->response_data_->status_code_ = 200;
    conn->response_data_->status_message_ = "OK";
    conn->response_data_->set_header("Content-Type", content_type);

    std::ostringstream size_stream;
    size_stream << file_size;
    conn->response_data_->set_header("Content-Length", size_stream.str());

    //conn->response_data_->body_data_.assign(file_content.begin(),
    //                                        file_content.end());

    //// Mark the connection as ready for writing
    //conn->conn_state_ = CONN_WRITING_RESPONSE;
    log(LOG_INFO, "StaticFileHandler: File ready to be served for client_fd %d",
        conn->client_fd_);
}

// 4. Clean up resources associated with the handler.
void StaticFileHandler::cleanup_handler(Connection* conn) {
    if (!conn || !conn->static_file_context_) {
        return;
    }

    log(LOG_DEBUG, "StaticFileHandler: Cleaning up handler for client_fd %d",
        conn->client_fd_);

    // Close the file descriptor
    if (conn->static_file_context_->file_fd_ >= 0) {
        close(conn->static_file_context_->file_fd_);
    }

    // Delete the context object
    delete conn->static_file_context_;
    conn->static_file_context_ = NULL;
}
