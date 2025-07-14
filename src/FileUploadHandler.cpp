#include "common.hpp"

// curl -v -F "file=@/webserv/var/www/files/cutecat.png"
// http://localhost:8080/upload

FileUploadHandler::FileUploadHandler() : AHandler() {}

FileUploadHandler::~FileUploadHandler() {}

Result FileUploadHandler::initialize_context(Connection* conn) {
    log(LOG_TRACE,
        "FileUploadHandler::initialize_context called for client_fd %d",
        conn->client_fd_);

    try {
        conn->file_upload_context_ = new FileUploadContext();
    } catch (const std::bad_alloc& e) {
        log(LOG_ERROR,
            "FileUploadHandler::initialize_context: Memory allocation failed "
            "for "
            "client_fd %d",
            conn->client_fd_);
        conn->status_ = INTERNAL_SERVER_ERROR;
        return ERROR;
    }

    return COMPLETE;
}

Result FileUploadHandler::setup_handler(Connection* conn) {
    log(LOG_TRACE, "FileUploadHandler::setup_handler called for client_fd %d",
        conn->client_fd_);

    // Extract boundary from content-type header
    std::string content_type = conn->request_data_->get_header("content-type");
    std::string& boundary = conn->file_upload_context_->multipart_context_.boundary_;
    boundary = multipart_parser_.extract_boundary(content_type);
    if (boundary.empty()) {
        log(LOG_ERROR, "No multipart boundary found for client_fd %d",
            conn->client_fd_);
        conn->status_ = BAD_REQUEST;
        return ERROR;
    }

    // Initialize multipart parser state
    conn->file_upload_context_->multipart_context_.state_ = FIND_INITIAL_BOUNDARY;
    conn->file_upload_context_->multipart_context_.is_file_part_ = false;
    conn->file_upload_context_->multipart_context_.part_headers_.clear();

    // Generate temporary file name
    std::string upload_dir = get_upload_directory(conn);

    // Do not create upload directory if it does not exist
    if (!ensure_upload_directory_exists(conn, upload_dir)) {
        return ERROR;  
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
    conn->file_upload_context_->file_fd_ = file_fd;
    conn->file_upload_context_->temp_path_ = temp_filename;

    return COMPLETE;
}

Result FileUploadHandler::handle(Connection* conn) {
    log(LOG_TRACE, "FileUploadHandler::handle() called for client_fd %d",
        conn->client_fd_);

    // Se o body_buffer_ está vazio, aguarde mais dados
    if (conn->request_data_->body_buffer_.empty()) {
        log(LOG_DEBUG, "[UPLOAD] body_buffer_ empty, waiting for more data.");
        return AGAIN;
    }

    log_buffer(LOG_FATAL, conn->request_data_->body_buffer_, "BODY BUFFER");

    ParseStatus status = multipart_parser_.parse_multipart(conn);
    if (status >= PARSE_ERROR) {
        conn->status_ = parse_status_to_response_status(status);
        return ERROR;
    }

    int file_fd = conn->file_upload_context_->file_fd_;

    // Write any parsed file data to the temp file
    Buffer& buffer = conn->file_upload_context_->upload_buffer_;

    log_buffer(LOG_WARNING, buffer, "UPLOAD BUFFER");

    if (!buffer.empty()) {
        // Log the current file offset before writing
        off_t before_offset = lseek(file_fd, 0, SEEK_CUR);
        log(LOG_DEBUG, "[UPLOAD] Offset before write: %jd", (intmax_t)before_offset);

        // Debug: log the exact data being written
        std::string debug_data(buffer.data(), buffer.readable_bytes());
        log(LOG_DEBUG, "[UPLOAD] About to write to temp file: length=%zu", buffer.readable_bytes());
    
        // Write incrementally to the temp file, without truncating or repositioning the pointer
        ssize_t written = buffer.write_to(file_fd);
        if (written < 0) {
            log(LOG_ERROR, "[UPLOAD] Error writing to temp file: %s", strerror(errno));
            conn->status_ = INTERNAL_SERVER_ERROR;
            return ERROR;
        }

        // Log the file offset after writing
        off_t after_offset = lseek(file_fd, 0, SEEK_CUR);
        log(LOG_DEBUG, "[UPLOAD] Offset after write: %jd, bytes written: %zd", (intmax_t)after_offset, written);
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

ParseStatus FileUploadHandler::check_permissions(Connection* conn) {
    log(LOG_TRACE,
        "FileUploadHandler::check_permissions called for client_fd %d",
        conn->client_fd_);

    std::string content_type = conn->request_data_->get_header("content-type");
    if (content_type.empty() || content_type.find("multipart/form-data") != 0) {
        return PARSE_UNSUPPORTED_MEDIA;
    }

    log(LOG_DEBUG,
        "FileUploadHandler: Permissions check passed for client_fd %d",
        conn->client_fd_);

    return PARSE_SUCCESS;
}

void FileUploadHandler::send_success_response(Connection* conn) {
    log(LOG_TRACE,
        "FileUploadHandler::send_success_response called for client_fd %d",
        conn->client_fd_);

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
    log(LOG_TRACE,
        "FileUploadHandler::get_upload_directory called for client_fd %d",
        conn->client_fd_);

    std::string upload_dir = parse_absolute_path(conn);

    // Extract just the directory part (remove any filename component)
    size_t last_slash = upload_dir.find_last_of('/');
    if (last_slash != std::string::npos) {
        upload_dir = upload_dir.substr(0, last_slash + 1);
    } else {
        // If no slash found, add one
        upload_dir += '/';
    }

    return upload_dir;
}

bool FileUploadHandler::ensure_upload_directory_exists(
    Connection* conn, const std::string& upload_dir) {
    log(LOG_TRACE,
        "FileUploadHandler::ensure_upload_directory_exists called for "
        "client_fd %d",
        conn->client_fd_);

    struct stat st;
    if (stat(upload_dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        if (access(upload_dir.c_str(), W_OK) != 0) {
            log(LOG_ERROR, "No write permission for upload directory: %s", upload_dir.c_str());
            conn->status_ = FORBIDDEN;
            return false;
        }
        return true;  // Directory exists and is writable
    }

    // Directory does not exist, do not create it
    log(LOG_ERROR, "Upload directory does not exist: %s", upload_dir.c_str());
    conn->status_ = INTERNAL_SERVER_ERROR;
    return false;
}

std::string FileUploadHandler::sanitize_filename(const std::string& filename) {
    log(LOG_TRACE, "FileUploadHandler::sanitize_filename called for '%s'",
        filename.c_str());

    // Remove path information
    std::string safe_filename = filename;
    size_t last_slash = safe_filename.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        safe_filename = safe_filename.substr(last_slash + 1);
    }

    // Remove potentially dangerous characters
    for (size_t i = 0; i < safe_filename.length(); ++i) {
        char c = safe_filename.at(i);
        // Keep alphanumeric, dash, underscore, dot
        if (!isalnum(c) && c != '-' && c != '_' && c != '.') {
            safe_filename.at(i) = '_';
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

// TODO: remove .tmp file after copying
bool FileUploadHandler::copy_temp_to_final_file(const std::string& temp_path,
                                                const std::string& final_path) {
    log(LOG_TRACE,
        "FileUploadHandler::copy_temp_to_final_file called for temp '%s' and "
        "final '%s'",
        temp_path.c_str(), final_path.c_str());

    // Debug: check if temp file exists and get its size
    struct stat temp_stat;
    if (stat(temp_path.c_str(), &temp_stat) == 0) {
        log(LOG_DEBUG, "Temp file exists, size: %ld bytes", temp_stat.st_size);
    } else {
        log(LOG_ERROR, "Temp file does not exist or stat failed: %s", strerror(errno));
    }

    int src_fd = open(temp_path.c_str(), O_RDONLY);
    if (src_fd < 0) {
        log(LOG_ERROR, "Failed to open temp file for reading: %s",
            strerror(errno));
        return false;
    }
    
    // Debug: read and log the temp file content before copying
    lseek(src_fd, 0, SEEK_SET);
    char debug_buf[4096];
    ssize_t debug_read = read(src_fd, debug_buf, sizeof(debug_buf));
    if (debug_read > 0) {
        std::string debug_content(debug_buf, debug_read);
        log(LOG_DEBUG, "Temp file content before copy: '%s' (length=%zd)", 
            debug_content.c_str(), debug_read);
    } else {
        log(LOG_ERROR, "Failed to read temp file for debug: %s", strerror(errno));
    }
    
    // Reset position for actual copy
    lseek(src_fd, 0, SEEK_SET);
    
    int dst_fd = open(final_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        log(LOG_ERROR, "Failed to open final file for writing: %s",
            strerror(errno));
        close(src_fd);
        return false;
    }
    char buf[4096];
    ssize_t bytes_read;
    ssize_t total_copied = 0;
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
        total_copied += bytes_read;
    }

    close(src_fd);
    close(dst_fd);

    log(LOG_DEBUG, "Copy completed: %zd bytes copied from temp to final", total_copied);

    if (bytes_read < 0) {
        log(LOG_ERROR, "Error reading from temp file: %s", strerror(errno));
        return false;
    }

    // Remove the temp file after successful copy
    if (unlink(temp_path.c_str()) != 0) {
        log(LOG_ERROR, "Failed to remove temp file '%s': %s", temp_path.c_str(), strerror(errno));
        // Not a fatal error, so do not return false
    } else {
        log(LOG_DEBUG, "Temp file '%s' removed after copy", temp_path.c_str());
    }

    return true;
}

void FileUploadHandler::cleanup_handler(Connection* conn) {
    if (conn->file_upload_context_->file_fd_ >= 0) {
        close(conn->file_upload_context_->file_fd_);
        conn->file_upload_context_->file_fd_ = -1;  // Mark as closed
    }

    if (conn->file_upload_context_) {
        delete conn->file_upload_context_;
        conn->file_upload_context_ = NULL;
    }

    log(LOG_DEBUG, "FileUploadHandler: Cleanup completed for client_fd %d",
        conn->client_fd_);

    return;
}
