#include "common.hpp"

FileDeleteHandler::FileDeleteHandler() : AHandler() {}

FileDeleteHandler::~FileDeleteHandler() {}

ResponseStatus FileDeleteHandler::handle_event(Connection* conn) {
    // This function is intentionally empty. 
    // All file deletion logic is handled synchronously in `setup_handler`. 
    // Since a DELETE request does not have a body to process, 
    // no further event-based handling is needed after the initial setup.
    (void)conn;
    return OK;
}

ResponseStatus FileDeleteHandler::check_permissions(Connection* conn) {
    // Check if we can access the target directory for deletion
    std::string file_path = parse_absolute_path(conn);
    if (file_path.empty()) {
        log(LOG_ERROR,
            "FileDeleteHandler: Failed to determine file path for client_fd %d",
            conn->client_fd_);
        return BAD_REQUEST;
    }

    // Get directory path
    size_t last_slash = file_path.find_last_of('/');
    std::string dir_path = (last_slash != std::string::npos)
                               ? file_path.substr(0, last_slash)
                               : ".";

    // Check if directory is writable (needed to delete files)
    if (access(dir_path.c_str(), W_OK) != 0) {
        log(LOG_ERROR,
            "FileDeleteHandler: No write permission for directory: %s",
            dir_path.c_str());
        return FORBIDDEN;
    }

    // Check if file exists and is accessible
    if (access(file_path.c_str(), F_OK) != 0) {
        if (errno == ENOENT) {
            log(LOG_ERROR, "FileDeleteHandler: File not found: %s",
                file_path.c_str());
            return NOT_FOUND;
        } else if (errno == EACCES) {
            log(LOG_ERROR, "FileDeleteHandler: Access denied to file: %s",
                file_path.c_str());
            return FORBIDDEN;
        } else {
            log(LOG_ERROR, "FileDeleteHandler: Error accessing file %s: %s",
                file_path.c_str(), strerror(errno));
            return INTERNAL_SERVER_ERROR;
        }
        return INTERNAL_SERVER_ERROR;
    }

    log(LOG_DEBUG,
        "FileDeleteHandler: Permissions check passed for client_fd %d",
        conn->client_fd_);

    return OK;  
}

ResponseStatus FileDeleteHandler::setup_handler(Connection* conn) {
    ResponseStatus status = OK;
    log(LOG_DEBUG, "FileDeleteHandler: Starting processing for client_fd %d",
        conn->client_fd_);

    // 1. Check for location redirects (same pattern as other handlers)
    status = process_location_redirect(conn);
    if (status != OK) {
        return status;  // Redirect response was set up, stop processing
    }

    // 2. Validate the DELETE request
    status = validate_delete_request(conn)
    if (status != OK) {
        return status;  
    }

    // 3. Extract file path from request
    std::string file_path;
    status = extract_file_path(conn, file_path);
    if (status != OK) {
        return status;  
    }

    // 4. Attempt to delete the file
    status = delete_file(conn, file_path);
    if (status == OK) {
        // Extract just the filename for the response
        size_t last_slash = file_path.find_last_of('/');
        std::string filename = (last_slash != std::string::npos)
                                   ? file_path.substr(last_slash + 1)
                                   : file_path;
        send_delete_success_response(conn, filename);
    }
    log(LOG_DEBUG, "FileDeleteHandler: Setup complete for client_fd %d",
        conn->client_fd_);
    return status;
}

void FileDeleteHandler::cleanup_handler(Connection* conn) {
    // No specific cleanup needed for file deletion
    log(LOG_DEBUG, "FileDeleteHandler: Cleanup complete for client_fd %d",
        conn->client_fd_);
}

ResponseStatus FileDeleteHandler::validate_delete_request(Connection* conn) {
    // 1. Basic connection validation
    if (!conn->request_data_ || !conn->response_data_) {
        return INTERNAL_SERVER_ERROR;
    }

    // 2. Location match validation
    if (!conn->location_match_) {
        return INTERNAL_SERVER_ERROR;
    }

    return OK;
}

ResponseStatus FileDeleteHandler::extract_file_path(Connection* conn,
                                          std::string& file_path) {
    // Use the same path resolution as other handlers
    file_path = parse_absolute_path(conn);

    if (file_path.empty()) {
        log(LOG_ERROR,
            "FileDeleteHandler: Failed to resolve file path for URI: %s",
            conn->request_data_->uri_.c_str());
        return BAD_REQUEST;
    }

    // Check if path ends with slash (directory)
    if (!file_path.empty() && file_path[file_path.length() - 1] == '/') {
        log(LOG_ERROR, "FileDeleteHandler: Cannot delete directory: %s",
            file_path.c_str());
        return FORBIDDEN;
    }

    // Basic path traversal protection (additional security)
    if (file_path.find("..") != std::string::npos) {
        log(LOG_ERROR, "FileDeleteHandler: Path traversal detected: %s",
            file_path.c_str());
        return FORBIDDEN;
    }

    log(LOG_DEBUG, "FileDeleteHandler: Resolved file path: %s",
        file_path.c_str());
    return OK;
}

ResponseStatus FileDeleteHandler::delete_file(Connection* conn,
                                    const std::string& file_path) {
    log(LOG_DEBUG, "FileDeleteHandler: Attempting to delete file: %s",
        file_path.c_str());

    // 1. Check if file exists and get file info
    struct stat file_stat;
    if (stat(file_path.c_str(), &file_stat) != 0) {
        if (errno == ENOENT) {
            log(LOG_INFO, "FileDeleteHandler: File not found: %s",
                file_path.c_str());
            return NOT_FOUND;
        } else if (errno == EACCES) {
            log(LOG_ERROR, "FileDeleteHandler: Access denied for file: %s",
                file_path.c_str());
            return FORBIDDEN;
        } else {
            log(LOG_ERROR, "FileDeleteHandler: Error accessing file %s: %s",
                file_path.c_str(), strerror(errno));
            return INTERNAL_SERVER_ERROR;
        }
    }

    // 2. Check if it's a regular file (not directory, device, etc.)
    if (!S_ISREG(file_stat.st_mode)) {
        log(LOG_ERROR, "FileDeleteHandler: Cannot delete non-regular file: %s",
            file_path.c_str());
        return FORBIDDEN;
    }

    // 3. Check file permissions (read permission as indicator of accessibility)
    if (access(file_path.c_str(), W_OK) != 0) {
        log(LOG_ERROR, "FileDeleteHandler: No write permission for file: %s",
            file_path.c_str());
        return FORBIDDEN;
    }

    // 4. Attempt to delete the file
    if (unlink(file_path.c_str()) != 0) {
        if (errno == EACCES || errno == EPERM) {
            log(LOG_ERROR,
                "FileDeleteHandler: Permission denied deleting file: %s",
                file_path.c_str());
            return FORBIDDEN;
        } else if (errno == ENOENT) {
            log(LOG_ERROR,
                "FileDeleteHandler: File disappeared during deletion: %s",
                file_path.c_str());
            return NOT_FOUND;
        } else if (errno == EBUSY) {
            log(LOG_ERROR, "FileDeleteHandler: File is busy, cannot delete: %s",
                file_path.c_str());
            return CONFLICT;
        } else {
            log(LOG_ERROR, "FileDeleteHandler: Failed to delete file %s: %s",
                file_path.c_str(), strerror(errno));
            return INTERNAL_SERVER_ERROR;
        }
    }

    log(LOG_INFO, "FileDeleteHandler: Successfully deleted file: %s",
        file_path.c_str());
    return OK;
}

void FileDeleteHandler::send_delete_success_response(
    Connection* conn, const std::string& filename) {
    HttpResponse* resp = conn->response_data_;

    // Use 204 No Content (nginx default for successful DELETE)
    resp->status_code_ = 204;
    resp->status_message_ = "No Content";

    // No body for 204 response
    resp->body_data_.clear();
    resp->content_length_ = 0;

    // Set standard headers
    resp->set_header("Content-Length", "0");
    resp->set_header("Server", "webserv/1.0");

    log(LOG_INFO,
        "FileDeleteHandler: Successfully sent 204 response for deleted file: "
        "%s",
        filename.c_str());
}
