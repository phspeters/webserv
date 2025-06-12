#include "webserv.hpp"

FileDeleteHandler::FileDeleteHandler() : AHandler() {}

FileDeleteHandler::~FileDeleteHandler() {}

void FileDeleteHandler::handle(Connection* conn) {

    log(LOG_DEBUG, "FileDeleteHandler: Starting processing for client_fd %d",
        conn->client_fd_);

    // 1. Check for location redirects (same pattern as other handlers)
    if (process_location_redirect(conn)) {
        return;  // Redirect response was set up, stop processing
    }

    // 2. Validate the DELETE request
    if (!validate_delete_request(conn)) {
        return; // Error response already set
    }

    // 3. Extract file path from request
    std::string file_path;
    if (!extract_file_path(conn, file_path)) {
        return; // Error response already set
    }

    // 4. Attempt to delete the file
    if (delete_file(conn, file_path)) {
        // Extract just the filename for the response
        size_t last_slash = file_path.find_last_of('/');
        std::string filename = (last_slash != std::string::npos) 
            ? file_path.substr(last_slash + 1) 
            : file_path;
        send_delete_success_response(conn, filename);
    }
    // Error response already set by delete_file() on failure

    conn->conn_state_ = codes::CONN_WRITING;
}

bool FileDeleteHandler::validate_delete_request(Connection* conn) {
    
    // 1. Basic connection validation
    if (!conn->request_data_ || !conn->response_data_) {
        ErrorHandler::generate_error_response(conn, codes::INTERNAL_SERVER_ERROR);
        return false;
    }

    // 2. Location match validation
    if (!conn->location_match_) {
        ErrorHandler::generate_error_response(conn, codes::INTERNAL_SERVER_ERROR);
        return false;
    }

    return true;
}

bool FileDeleteHandler::extract_file_path(Connection* conn, std::string& file_path) {
    // Use the same path resolution as other handlers
    file_path = parse_absolute_path(conn);
    
    if (file_path.empty()) {
        log(LOG_ERROR, "FileDeleteHandler: Failed to resolve file path for URI: %s", 
            conn->request_data_->uri_.c_str());
        ErrorHandler::generate_error_response(conn, codes::BAD_REQUEST);
        return false;
    }

    // Check if path ends with slash (directory)
    if (!file_path.empty() && file_path[file_path.length() - 1] == '/') {
        log(LOG_ERROR, "FileDeleteHandler: Cannot delete directory: %s", file_path.c_str());
        ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
        return false;
    }

    // Basic path traversal protection (additional security)
    if (file_path.find("..") != std::string::npos) {
        log(LOG_ERROR, "FileDeleteHandler: Path traversal detected: %s", file_path.c_str());
        ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
        return false;
    }

    log(LOG_DEBUG, "FileDeleteHandler: Resolved file path: %s", file_path.c_str());
    return true;
}

bool FileDeleteHandler::delete_file(Connection* conn, const std::string& file_path) {
    log(LOG_DEBUG, "FileDeleteHandler: Attempting to delete file: %s", file_path.c_str());
    
    // 1. Check if file exists and get file info
    struct stat file_stat;
    if (stat(file_path.c_str(), &file_stat) != 0) {
        if (errno == ENOENT) {
            log(LOG_INFO, "FileDeleteHandler: File not found: %s", file_path.c_str());
            ErrorHandler::generate_error_response(conn, codes::NOT_FOUND);
        } else if (errno == EACCES) {
            log(LOG_ERROR, "FileDeleteHandler: Access denied for file: %s", file_path.c_str());
            ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
        } else {
            log(LOG_ERROR, "FileDeleteHandler: Error accessing file %s: %s", 
                file_path.c_str(), strerror(errno));
            ErrorHandler::generate_error_response(conn, codes::INTERNAL_SERVER_ERROR);
        }
        return false;
    }
    
    // 2. Check if it's a regular file (not directory, device, etc.)
    if (!S_ISREG(file_stat.st_mode)) {
        log(LOG_ERROR, "FileDeleteHandler: Cannot delete non-regular file: %s", file_path.c_str());
        ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
        return false;
    }
    
    // 3. Check file permissions (read permission as indicator of accessibility)
    if (access(file_path.c_str(), W_OK) != 0) {
        log(LOG_ERROR, "FileDeleteHandler: No write permission for file: %s", file_path.c_str());
        ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
        return false;
    }
    
    // 4. Attempt to delete the file
    if (unlink(file_path.c_str()) != 0) {
        if (errno == EACCES || errno == EPERM) {
            log(LOG_ERROR, "FileDeleteHandler: Permission denied deleting file: %s", file_path.c_str());
            ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
        } else if (errno == ENOENT) {
            log(LOG_ERROR, "FileDeleteHandler: File disappeared during deletion: %s", file_path.c_str());
            ErrorHandler::generate_error_response(conn, codes::NOT_FOUND);
        } else if (errno == EBUSY) {
            log(LOG_ERROR, "FileDeleteHandler: File is busy, cannot delete: %s", file_path.c_str());
            ErrorHandler::generate_error_response(conn, codes::CONFLICT);
        } else {
            log(LOG_ERROR, "FileDeleteHandler: Failed to delete file %s: %s", 
                file_path.c_str(), strerror(errno));
            ErrorHandler::generate_error_response(conn, codes::INTERNAL_SERVER_ERROR);
        }
        return false;
    }
    
    log(LOG_INFO, "FileDeleteHandler: Successfully deleted file: %s", file_path.c_str());
    return true;
}

void FileDeleteHandler::send_delete_success_response(Connection* conn, const std::string& filename) {
    HttpResponse* resp = conn->response_data_;
    
    // Use 204 No Content (nginx default for successful DELETE)
    resp->status_code_ = 204;
    resp->status_message_ = "No Content";
    
    // No body for 204 response
    resp->body_.clear();
    resp->content_length_ = 0;
    
    // Set standard headers
    resp->set_header("Content-Length", "0");
    resp->set_header("Server", "webserv/1.0");
    
    log(LOG_INFO, "FileDeleteHandler: Successfully sent 204 response for deleted file: %s", 
        filename.c_str());
}