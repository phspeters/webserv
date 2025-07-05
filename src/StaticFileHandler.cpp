#include "common.hpp"

StaticFileHandler::StaticFileHandler() {}

StaticFileHandler::~StaticFileHandler() {}

Result StaticFileHandler::check_permissions(Connection* conn) {
    log(LOG_DEBUG, "check_permissions: Checking permissions for client_fd %d",
        conn->client_fd_);

    if (conn->request_data_->method_ != "GET") {
        conn->response_data_->set_error_header("Allow", "GET");
        conn->status_ = METHOD_NOT_ALLOWED;
        return ERROR;
    }

    if (process_location_redirect(conn)) {
        log(LOG_DEBUG,
            "check_permissions: Processed location redirect for client_fd %d",
            conn->client_fd_);
        return COMPLETE;
    }

    std::string absolute_path = parse_absolute_path(conn);
    if( absolute_path.empty() ) {
        log(LOG_ERROR, "check_permissions: Empty absolute path for client_fd %d",
            conn->client_fd_);
        conn->status_ = INTERNAL_SERVER_ERROR;
        return ERROR;   
    }

    if (absolute_path[absolute_path.length() - 1] == '/') {
        if (process_directory_redirect(conn, absolute_path)) {
            return COMPLETE;
        }

        bool need_autoindex = false;
        if (process_directory_index(conn, absolute_path, need_autoindex)) {
            if (need_autoindex) {
                // The request URI points to a directory.
                // The server configuration for that directory does not find an index file AND has autoindex on;
                generate_directory_listing(conn, absolute_path);
                return COMPLETE;
            }
        } else {
            conn->status_ = FORBIDDEN;
            return ERROR;
        }
        std::string index_file = conn->location_match_->index_;
        absolute_path = absolute_path + index_file;
    }

    struct stat file_info;
    if (stat(absolute_path.c_str(), &file_info) == -1) {
        if (errno == ENOENT || errno == ENOTDIR) {
            conn->status_ = NOT_FOUND;
        }
        else if (errno == EACCES) {
            conn->status_ = FORBIDDEN;
        } else {
            conn->status_ = INTERNAL_SERVER_ERROR;
        }   
        return ERROR;
    }

    if (!S_ISREG(file_info.st_mode)) {
        conn->status_ = FORBIDDEN;
        return ERROR;
    }

    conn->static_file_context_->absolute_path_ = absolute_path;
    log(LOG_DEBUG,
        "check_permissions: Permissions check passed for client_fd %d",
        conn->client_fd_);

    return COMPLETE;
}


Result StaticFileHandler::setup_handler(Connection* conn) {
    log(LOG_DEBUG, "setup_handler: Setting up handler for client_fd %d",
        conn->client_fd_);

    std::string absolute_path = conn->static_file_context_->absolute_path_;

    int fd = open(absolute_path.c_str(), O_RDONLY);
    if (fd == -1) {
        if (errno == ENOENT) {
            conn->status_ = NOT_FOUND;
        } else if (errno == EACCES) {
            conn->status_ = FORBIDDEN;
        } else {
            conn->status_ = INTERNAL_SERVER_ERROR;
        }
        return ERROR;
    }

    // Create and populate the context for this connection
    struct stat file_info;
    conn->static_file_context_ = new StaticFileContext();
    conn->static_file_context_->file_fd_ = fd;
    conn->static_file_context_->bytes_to_send_ = file_info.st_size;

    log(LOG_INFO,
        "setup_handler: Setup complete for client_fd %d, file_fd %d",
        conn->client_fd_, fd);
    
    return COMPLETE; 
}

// 3. Main logic: read the file and prepare the response.
Result StaticFileHandler::handle_static_file_read(Connection* conn) {
    log(LOG_DEBUG, "handle_event: Handling event for client_fd %d",
        conn->client_fd_);

    if (!conn->static_file_context_ ||
        conn->static_file_context_->file_fd_ < 0) {
            log(LOG_ERROR,
                "StaticFileHandler: Invalid static file context for client_fd %d",
                conn->client_fd_);
            conn->status_ = INTERNAL_SERVER_ERROR;
        return ERROR;
    }

    int fd = conn->static_file_context_->file_fd_;
    size_t file_size = conn->static_file_context_->bytes_to_send_;


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

    log(LOG_INFO, "StaticFileHandler: File ready to be served for client_fd %d",
        conn->client_fd_);

    return COMPLETE;
}

void StaticFileHandler::cleanup_handler(Connection* conn) {
    
    if (!conn || !conn->static_file_context_) {
        log(LOG_DEBUG, "StaticFileHandler: No cleanup needed for client_fd %d",
            conn ? conn->client_fd_ : -1);
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
