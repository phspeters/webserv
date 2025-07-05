#include "common.hpp"

// curl -v -F "file=@files/cutecat.png" http://localhost:8080/upload

FileUploadHandler::FileUploadHandler() : AHandler() {}

FileUploadHandler::~FileUploadHandler() {}

bool FileUploadHandler::process_trailing_slash_redirect(Connection* conn) {
    std::string uri = conn->request_data_->uri_;
    const Location* location = conn->location_match_;

    // If location path ends with / but URI doesn't, redirect to add slash
    if (!location->path_.empty() &&
        location->path_[location->path_.length() - 1] == '/' && !uri.empty() &&
        uri[uri.length() - 1] != '/') {
        conn->response_data_->set_header("Location", uri + "/");
        conn->status_ = MOVED_PERMANENTLY;
        return true;
    }
    return false;
}

void FileUploadHandler::send_success_response(Connection* conn) {
    HttpResponse* resp = conn->response_data_;
    resp->status_code_ = 201;
    resp->status_message_ = "Created";
    resp->content_type_ = "text/html";
    std::string body =
        "<html><body><h1>Upload Successful</h1><p>Your file has been "
        "uploaded successfully.</p></body></html>";
    resp->body_data_.assign(body.begin(), body.end());
    resp->content_length_ = resp->body_data_.size();
}

std::string FileUploadHandler::get_upload_directory(Connection* conn) {
    std::string base_path = parse_absolute_path(conn);

    // Extract just the directory part (remove any filename component)
    size_t last_slash = base_path.find_last_of('/');
    if (last_slash != std::string::npos) {
        base_path = base_path.substr(0, last_slash + 1);
    } else {
        // If no slash found, add one
        base_path += '/';
    }

    // Add uploads subdirectory
    std::string upload_dir = base_path + "uploads/";

    return upload_dir;
}

bool FileUploadHandler::ensure_upload_directory_exists(
    Connection* conn, const std::string& upload_dir) {
    struct stat st;
    if (stat(upload_dir.c_str(), &st) == 0) {
        return true;  // Directory already exists
    }

    return create_directory_recursive(conn, upload_dir);
}

bool FileUploadHandler::create_directory_recursive(Connection* conn,
                                                   const std::string& path) {
    size_t pos = 0;
    while ((pos = path.find('/', pos + 1)) != std::string::npos) {
        if (pos > 0) {
            std::string parent_dir = path.substr(0, pos);
            if (mkdir(parent_dir.c_str(), 0755) != 0 && errno != EEXIST) {
                if (errno == EACCES || errno == EPERM) {
                    conn->status_ = FORBIDDEN;
                } else {
                    conn->status_ = INTERNAL_SERVER_ERROR;
                }
                return false;
            }
        }
    }

    // Create final directory
    if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
        if (errno == EACCES || errno == EPERM) {
            conn->status_ = FORBIDDEN;
        } else {
            conn->status_ = INTERNAL_SERVER_ERROR;
        }
        return false;
    }
    return true;
}

std::string FileUploadHandler::sanitize_filename(const std::string& filename) {
    // Remove path information
    std::string safe_filename = filename;
    size_t last_slash = safe_filename.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        safe_filename = safe_filename.substr(last_slash + 1);
    }

    // Remove potentially dangerous characters
    for (size_t i = 0; i < safe_filename.length(); ++i) {
        char c = safe_filename[i];
        // Keep alphanumeric, dash, underscore, dot
        if (!isalnum(c) && c != '-' && c != '_' && c != '.') {
            safe_filename[i] = '_';
        }
    }

    // Handle edge cases
    if (safe_filename.empty() || safe_filename == "." ||
        safe_filename == "..") {
        return "upload_file";
    }

    // Limit filename length for filesystem compatibility
    if (safe_filename.length() > 255) {
        safe_filename = safe_filename.substr(0, 255);
    }

    return safe_filename;
}

bool FileUploadHandler::copy_temp_to_final_file(const std::string& temp_path,
                                                const std::string& final_path) {
    int src_fd = open(temp_path.c_str(), O_RDONLY);
    if (src_fd < 0) {
        log(LOG_ERROR, "Failed to open temp file for reading: %s",
            strerror(errno));
        return false;
    }
    int dst_fd = open(final_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        log(LOG_ERROR, "Failed to open final file for writing: %s",
            strerror(errno));
        close(src_fd);
        return false;
    }
    char buf[4096];
    ssize_t bytes_read;
    while ((bytes_read = read(src_fd, buf, sizeof(buf))) > 0) {
        ssize_t total_written = 0;
        while (total_written < bytes_read) {
            ssize_t bytes_written =
                write(dst_fd, buf + total_written, bytes_read - total_written);
            if (bytes_written < 0) {
                log(LOG_ERROR, "Error writing to final file: %s",
                    strerror(errno));
                close(src_fd);
                close(dst_fd);
                return false;
            }
            total_written += bytes_written;
        }
    }
    if (bytes_read < 0) {
        log(LOG_ERROR, "Error reading from temp file: %s", strerror(errno));
        close(src_fd);
        close(dst_fd);
        return false;
    }
    close(src_fd);
    close(dst_fd);
    return true;
}

Result FileUploadHandler::check_permissions(Connection* conn) {
    std::string content_length =
        conn->request_data_->get_header("content-length");
    if (content_length.empty()) {
        conn->status_ = BAD_REQUEST;
        return ERROR;
    }
    std::string content_type = conn->request_data_->get_header("content-type");
    if (content_type.empty() || content_type.find("multipart/form-data") != 0) {
        conn->status_ = UNSUPPORTED_MEDIA_TYPE;
        return ERROR;
    }

    std::string boundary =  conn->file_upload_context_->multipart_context_.boundary_;
    boundary = multipart_parser_.extract_boundary(content_type);
    if (boundary.empty()) {
        log(LOG_ERROR, "No multipart boundary found for client_fd %d",
            conn->client_fd_);
        conn->status_ = BAD_REQUEST;
        return ERROR;
    }

     // Redirect if needed
    if (process_trailing_slash_redirect(conn)) {
        return COMPLETE;
    }

    log(LOG_DEBUG,
        "FileUploadHandler: Permissions check passed for client_fd %d",
        conn->client_fd_);

    conn->status_ = OK;
    return COMPLETE;
}

Result FileUploadHandler::setup_handler(Connection* conn) {
    // Generate temporary file name
    std::string upload_dir = get_upload_directory(conn);

    if (!ensure_upload_directory_exists(conn, upload_dir)) {
        return ERROR;  // Directory creation failed
    }

    std::stringstream ss;
    ss << conn->client_fd_;
    std::string temp_filename = upload_dir + "upload_" + ss.str() + ".tmp";
    int file_fd =
        open(temp_filename.c_str(), O_WRONLY | O_CREAT | O_NONBLOCK, 0644);
    if (file_fd < 0) {
        conn->status_ = INTERNAL_SERVER_ERROR;
        return ERROR;
    }

    // Initialize context
    conn->file_upload_context_ = new FileUploadContext();
    conn->file_upload_context_->file_fd_ = file_fd;
    conn->file_upload_context_->temp_path_ = temp_filename;
    log(LOG_INFO,
        "FileUploadHandler: Setup complete for client_fd %d, file_fd %d",
        conn->client_fd_, file_fd);

    return COMPLETE;
}

// TODO: Handle multipart parsing inside handle_event
Result FileUploadHandler::handle_file_upload_write(Connection* conn) {
    // Consume from upload_buffer_ and write to temporary file
    if (!conn->file_upload_context_) {
        log(LOG_FATAL,
            "FileUploadHandler: No file upload context for client_fd %d",
            conn->client_fd_);
        conn->status_ = INTERNAL_SERVER_ERROR;
        return ERROR;
    }

    ParseStatus status = multipart_parser_.parse_multipart(conn);
    if (status >= PARSE_ERROR) {
        conn->status_ = ErrorHandler::parse_status_to_response_status(status);
        return ERROR;
    }

    // Write any parsed file data to the temp file
    Buffer& buffer = conn->file_upload_context_->upload_buffer_;
    int file_fd = conn->file_upload_context_->file_fd_;
    if (!buffer.empty()) {
        ssize_t written = buffer.write_to(file_fd);
        if (written < 0) {
            conn->status_ = INTERNAL_SERVER_ERROR;
            return ERROR;
        }
        log(LOG_DEBUG, "FileUploadHandler: Wrote %zd bytes to temp file fd %d",
            written, file_fd);
    }

    if (status == PARSE_INCOMPLETE) {
        return AGAIN;
    }

    if (conn->file_upload_context_->upload_complete) {
        // Close the temp file before copying
        int file_fd = conn->file_upload_context_->file_fd_;
        if (file_fd >= 0) {
            close(file_fd);
            conn->file_upload_context_->file_fd_ = -1;  // Mark as closed
        }

        // Determine final file path
        std::string filename = conn->file_upload_context_->filename_;
        if (filename.empty()) {
            filename = "upload_file";
        }
        filename = sanitize_filename(filename);
        std::string final_path = get_upload_directory(conn) + filename;

        // Copy temp file to final destination
        if (!copy_temp_to_final_file(conn->file_upload_context_->temp_path_,
                                     final_path)) {
            conn->status_ = INTERNAL_SERVER_ERROR;
            return ERROR;
        }
        log(LOG_INFO,
            "FileUploadHandler: Copied temp file to final file '%s' for "
            "client_fd %d",
            final_path.c_str(), conn->client_fd_);

        send_success_response(conn);
        return COMPLETE;
    }
    return AGAIN;
}

void FileUploadHandler::cleanup_handler(Connection* conn) {
    delete conn->file_upload_context_;
    conn->file_upload_context_ = NULL;

    return;
}
