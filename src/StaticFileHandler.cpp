#include "common.hpp"

StaticFileHandler::StaticFileHandler() {}

StaticFileHandler::~StaticFileHandler() {}

Result StaticFileHandler::handle(Connection* conn) {
    log(LOG_DEBUG, "StaticFileHandler: Handling request for client_fd %d",
        conn->client_fd_);

    conn->static_file_context_ = new StaticFileContext();

    // Validate HTTP method
    Result result = validate_method(conn);
    if (result != COMPLETE) {
        return result;
    }

    // Handle location redirects
    result = handle_location_redirect(conn);
    if (result != COMPLETE) {
        return result;
    }

    // Parse and validate the absolute path
    std::string absolute_path;
    result = resolve_absolute_path(conn, absolute_path);
    if (result != COMPLETE) {
        return result;
    }

    // Handle directory requests (index files, autoindex, etc.)
    result = handle_directory_request(conn, absolute_path);
    if (result != COMPLETE) {
        return result;
    }

    // Validate file existence and permissions
    result = validate_file_access(conn, absolute_path);
    if (result != COMPLETE) {
        return result;
    }

    // Open file and prepare response
    return prepare_file_response(conn, absolute_path);
}

Result StaticFileHandler::validate_method(Connection* conn) {
    if (conn->request_data_->method_ != "GET") {
        conn->response_data_->set_error_header("Allow", "GET");
        conn->status_ = METHOD_NOT_ALLOWED;
        return ERROR;
    }
    return COMPLETE;
}

Result StaticFileHandler::handle_location_redirect(Connection* conn) {
    if (process_location_redirect(conn)) {
        log(LOG_DEBUG,
            "StaticFileHandler: Processed location redirect for client_fd %d",
            conn->client_fd_);
        return AGAIN;  // Indicates that the request has been redirected
    }
    return COMPLETE;
}

Result StaticFileHandler::resolve_absolute_path(Connection* conn,
                                                std::string& absolute_path) {
    absolute_path = parse_absolute_path(conn);
    if (absolute_path.empty()) {
        log(LOG_ERROR,
            "StaticFileHandler: Empty absolute path for client_fd %d",
            conn->client_fd_);
        conn->status_ = INTERNAL_SERVER_ERROR;
        return ERROR;
    }
    return COMPLETE;
}

Result StaticFileHandler::handle_directory_request(Connection* conn,
                                                   std::string& absolute_path) {
    if (absolute_path[absolute_path.length() - 1] != '/') {
        return COMPLETE;
    }

    if (process_directory_redirect(conn, absolute_path)) {
        return COMPLETE;
    }

    bool need_autoindex = false;
    if (process_directory_index(conn, absolute_path, need_autoindex)) {
        if (need_autoindex) {
            generate_directory_listing(conn, absolute_path);
            return COMPLETE;
        }
    } else {
        conn->status_ = FORBIDDEN;
        return ERROR;
    }

    std::string index_file = conn->location_match_->index_;
    absolute_path = absolute_path + index_file;
    return COMPLETE;
}

Result StaticFileHandler::validate_file_access(
    Connection* conn, const std::string& absolute_path) {
    struct stat file_info;
    if (stat(absolute_path.c_str(), &file_info) == -1) {
        if (errno == ENOENT || errno == ENOTDIR) {
            conn->status_ = NOT_FOUND;
        } else if (errno == EACCES) {
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
        "StaticFileHandler: Permissions check passed for client_fd %d",
        conn->client_fd_);
    return COMPLETE;
}

Result StaticFileHandler::prepare_file_response(
    Connection* conn, const std::string& absolute_path) {
    int fd = open(absolute_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd == -1) {
        return handle_file_open_error(conn);
    }

    // Get file stats
    struct stat file_info;
    if (fstat(fd, &file_info) == -1) {
        close(fd);
        conn->status_ = INTERNAL_SERVER_ERROR;
        return ERROR;
    }

    conn->static_file_context_->file_fd_ = fd;

    set_response_headers(conn, file_info);

    log(LOG_INFO, "StaticFileHandler: File ready to be served for client_fd %d",
        conn->client_fd_);
    return COMPLETE;
}

Result StaticFileHandler::handle_file_open_error(Connection* conn) {
    if (errno == ENOENT) {
        conn->status_ = NOT_FOUND;
    } else if (errno == EACCES) {
        conn->status_ = FORBIDDEN;
    } else {
        conn->status_ = INTERNAL_SERVER_ERROR;
    }
    return ERROR;
}

void StaticFileHandler::set_response_headers(Connection* conn,
                                             const struct stat& file_info) {
    std::string content_type =
        determine_content_type(conn->static_file_context_->absolute_path_);

    conn->response_data_->status_code_ = 200;
    conn->response_data_->status_message_ = "OK";
    conn->response_data_->set_header("Content-Type", content_type);

    std::ostringstream size_stream;
    size_stream << file_info.st_size;
    conn->response_data_->set_header("Content-Length", size_stream.str());

    conn->response_data_->body_fd_ = conn->static_file_context_->file_fd_;
}

std::string StaticFileHandler::determine_content_type(const std::string& path) {
    log(LOG_FATAL, "StaticFileHandler: Determining content type for path: %s",
        path.c_str());
    size_t dot_pos = path.find_last_of('.');
    if (dot_pos == std::string::npos) {
        log(LOG_WARNING, "StaticFileHandler: No extension found for path: %s",
            path.c_str());
        return "application/octet-stream";
    }

    std::string ext = path.substr(dot_pos + 1);
    if (ext == "html" || ext == "htm") {
        return "text/html";
    } else if (ext == "css") {
        return "text/css";
    } else if (ext == "js") {
        return "application/javascript";
    } else if (ext == "png") {
        return "image/png";
    } else if (ext == "jpg" || ext == "jpeg") {
        return "image/jpeg";
    } else if (ext == "gif") {
        return "image/gif";
    } else if (ext == "txt") {
        return "text/plain";
    } else {
        return "application/octet-stream";
    }
}

void StaticFileHandler::cleanup_handler(Connection* conn) {
    if (!conn) {
        log(LOG_FATAL,
            "StaticFileHandler: Cleanup called with NULL connection");
        return;
    }

    log(LOG_DEBUG, "StaticFileHandler: Cleaning up handler for client_fd %d",
        conn->client_fd_);

    // Close the file descriptor
    if (conn->static_file_context_->file_fd_ >= 0) {
        close(conn->static_file_context_->file_fd_);
        conn->static_file_context_->file_fd_ = -1;
    }

    // Delete the context object
    if (conn->static_file_context_) {
        delete conn->static_file_context_;
        conn->static_file_context_ = NULL;
    }
}
