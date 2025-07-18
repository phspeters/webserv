#include "common.hpp"

FileDeleteHandler::FileDeleteHandler() : AHandler() {}

FileDeleteHandler::~FileDeleteHandler() {}

Result FileDeleteHandler::initialize_context(Connection* conn) {
    log(LOG_TRACE,
        "FileDeleteHandler::initialize_context called for client_fd %d",
        conn->client_fd_);
    return COMPLETE;
}

ParseStatus FileDeleteHandler::check_permissions(Connection* conn) {
    log(LOG_TRACE,
        "FileDeleteHandler::check_permissions called for client_fd %d",
        conn->client_fd_);

    std::string file_path = parse_absolute_path(conn);

    if (file_path.empty()) {
        log(LOG_ERROR,
            "FileDeleteHandler: check_permissions: Could not resolve path for "
            "URI: %s",
            conn->request_data_->uri_.c_str());
        return PARSE_ERROR;
    }

    // Security: Do not allow deleting directories via this handler
    if (file_path[file_path.length() - 1] == '/') {
        log(LOG_ERROR,
            "FileDeleteHandler: check_permissions: Attempt to delete "
            "directory: %s",
            file_path.c_str());
        return PARSE_FORBIDDEN;
    }

    // Use stat() to check file existence and type in one system call
    struct stat file_stat;
    if (stat(file_path.c_str(), &file_stat) != 0) {
        if (errno == ENOENT) {
            log(LOG_INFO,
                "FileDeleteHandler: check_permissions: File to delete not "
                "found: %s",
                file_path.c_str());
            return PARSE_NOT_FOUND;
        } else if (errno == EACCES) {
            log(LOG_ERROR,
                "FileDeleteHandler: check_permissions: Permission denied to "
                "stat file: %s",
                file_path.c_str());
            return PARSE_FORBIDDEN;
        } else {
            log(LOG_ERROR,
                "FileDeleteHandler: check_permissions: stat error for %s: %s",
                file_path.c_str(), strerror(errno));
            return PARSE_INTERNAL_ERROR;
        }
    }

    // Ensure we are deleting a regular file
    if (!S_ISREG(file_stat.st_mode)) {
        log(LOG_ERROR,
            "FileDeleteHandler: check_permissions: Attempt to delete "
            "non-regular file: %s",
            file_path.c_str());
        return PARSE_FORBIDDEN;
    }

    // Get parent directory path to check for write permissions
    size_t last_slash = file_path.find_last_of('/');
    std::string dir_path = (last_slash != std::string::npos)
                               ? file_path.substr(0, last_slash)
                               : ".";

    if (access(dir_path.c_str(), W_OK) != 0) {
        log(LOG_ERROR,
            "FileDeleteHandler: check_permissions: No write permission on "
            "directory: %s",
            dir_path.c_str());
        return PARSE_FORBIDDEN;
    }

    log(LOG_DEBUG,
        "FileDeleteHandler: check_permissions: All permission checks passed "
        "for client_fd %d",
        conn->client_fd_);
    return PARSE_SUCCESS;
}

Result FileDeleteHandler::setup_handler(Connection* conn) {
    log(LOG_TRACE, "FileDeleteHandler::setup_handler called for client_fd %d",
        conn->client_fd_);
    return COMPLETE;  // No setup needed for file deletion
}

Result FileDeleteHandler::handle(Connection* conn) {
    log(LOG_TRACE, "FileDeleteHandler::handle called for client_fd %d",
        conn->client_fd_);

    std::string file_path = parse_absolute_path(conn);

    // Attempt to delete the file
    if (!delete_file(conn, file_path)) {
        log(LOG_ERROR,
            "FileDeleteHandler: setup_handler: Failed to delete file for "
            "client_fd %d",
            conn->client_fd_);
        return ERROR;
    }

    // Use 204 No Content (nginx default for successful DELETE)

    conn->response_data_->status_code_ = 204;
    conn->response_data_->set_header("Content-Length", "0");
    conn->response_data_->set_header("Server", "webserv/4.2");
    conn->response_data_->status_message_ = "No Content";
    conn->response_data_->content_length_ = 0;
    conn->response_data_->body_data_.clear();

    return COMPLETE;
}

bool FileDeleteHandler::delete_file(Connection* conn,
                                    const std::string& file_path) {
    log(LOG_DEBUG,
        "FileDeleteHandler::delete_file called for client %d with path %s",
        conn->client_fd_, file_path.c_str());

    // Attempt to delete the file
    if (std::remove(file_path.c_str()) != 0) {
        if (errno == EACCES || errno == EPERM) {
            log(LOG_ERROR,
                "FileDeleteHandler: Permission denied deleting file: %s",
                file_path.c_str());
            conn->status_ = FORBIDDEN;
        } else if (errno == ENOENT) {
            log(LOG_ERROR,
                "FileDeleteHandler: File disappeared during deletion: %s",
                file_path.c_str());
            conn->status_ = NOT_FOUND;
        } else if (errno == EBUSY) {
            log(LOG_ERROR, "FileDeleteHandler: File is busy, cannot delete: %s",
                file_path.c_str());
            conn->status_ = CONFLICT;
        } else {
            log(LOG_ERROR, "FileDeleteHandler: Failed to delete file %s: %s",
                file_path.c_str(), strerror(errno));
            conn->status_ = INTERNAL_SERVER_ERROR;
        }
        return false;
    }

    log(LOG_INFO, "FileDeleteHandler: Successfully deleted file: %s",
        file_path.c_str());
    return true;
}

void FileDeleteHandler::cleanup_handler(Connection* conn) {
    // No specific cleanup needed for file deletion
    log(LOG_DEBUG, "FileDeleteHandler: Cleanup complete for client_fd %d",
        conn->client_fd_);
}
