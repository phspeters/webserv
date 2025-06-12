#include "webserv.hpp"

// curl -v -F "file=@files/cutecat.png" http://localhost:8080/upload

FileUploadHandler::FileUploadHandler() : AHandler() {}

FileUploadHandler::~FileUploadHandler() {}

void FileUploadHandler::handle(Connection* conn) {
    log(LOG_DEBUG, "FileUploadHandler: Starting processing for client_fd %d",
        conn->client_fd_);

    if (process_location_redirect(conn) ||
        process_trailing_slash_redirect(conn)) {
        return;
    }

    if (!conn->location_match_) {
        ErrorHandler::generate_error_response(conn, codes::INTERNAL_SERVER_ERROR);
        return;
    }

    std::string boundary;
    if (!validate_request(conn, boundary)) {
        return;
    }

    if (parse_multipart_form_data(conn, boundary)) {
        send_success_response(conn);
    } else {
        ErrorHandler::generate_error_response(conn, codes::BAD_REQUEST);
    }

    conn->conn_state_ = codes::CONN_WRITING;
}

bool FileUploadHandler::process_trailing_slash_redirect(Connection* conn) {
    std::string uri = conn->request_data_->uri_;
    const Location* location = conn->location_match_;

    // If location path ends with / but URI doesn't, redirect to add slash
    if (!location->path_.empty() && 
        location->path_[location->path_.length() - 1] == '/' &&
        !uri.empty() && uri[uri.length() - 1] != '/') {
        
        ErrorHandler::generate_error_response(conn, codes::MOVED_PERMANENTLY);
        
        conn->response_data_->set_header("Location", uri + "/");


        return true;
    }
    return false;
}

bool FileUploadHandler::validate_request(Connection* conn,
                                         std::string& boundary) {
    if (!conn->request_data_ || !conn->response_data_) {
        ErrorHandler::generate_error_response(conn, codes::INTERNAL_SERVER_ERROR);
        return false;
    }

    std::string content_length =
        conn->request_data_->get_header("content-length");    
    if (content_length.empty()) {
        ErrorHandler::generate_error_response(conn, codes::BAD_REQUEST);
        return false;
    }
    // CHECK - Modification CAROL   
    if (conn->request_data_->body_.size() > conn->virtual_server_->client_max_body_size_) {
        ErrorHandler::generate_error_response(conn, codes::PAYLOAD_TOO_LARGE);
        return false;
    }

    std::string content_type = conn->request_data_->get_header("content-type");
    if (content_type.empty() || content_type.find("multipart/form-data") != 0) {
        ErrorHandler::generate_error_response(conn, codes::UNSUPPORTED_MEDIA_TYPE);
        return false;
    }

    boundary = extract_boundary(content_type);
    if (boundary.empty()) {
        ErrorHandler::generate_error_response(conn, codes::BAD_REQUEST);
        return false;
    }

    return true;
}

void FileUploadHandler::send_success_response(Connection* conn) {
    HttpResponse* resp = conn->response_data_;
    resp->status_code_ = 201;
    resp->status_message_ = "Created";
    resp->content_type_ = "text/html";
    std::string body =
        "<html><body><h1>Upload Successful</h1><p>Your file has been "
        "uploaded successfully.</p></body></html>";
    resp->body_.assign(body.begin(), body.end());
    resp->content_length_ = resp->body_.size();
}

bool FileUploadHandler::parse_multipart_form_data(Connection* conn,
                                                  const std::string& boundary) {
    std::string body(conn->request_data_->body_.begin(),
                     conn->request_data_->body_.end());
    std::string full_boundary = "--" + boundary;
    std::string end_boundary = full_boundary + "--";

    size_t pos = 0;
    bool file_found = false;

    while (pos < body.length()) {
        pos = body.find(full_boundary, pos);
        if (pos == std::string::npos) {
            break;
        }

        pos += full_boundary.length();
        if (pos >= body.length()) {
            break;
        }

        // Skip CRLF after boundary
        if (pos + 2 <= body.length() && body.substr(pos, 2) == "\r\n") {
            pos += 2;
        }

        // Check for end boundary
        if (pos + 2 <= body.length() && body.substr(pos, 2) == "--") {
            break;
        }

        if (!process_part(conn, body, full_boundary, end_boundary, pos,
                          file_found)) {
            return false;
        }

        // Safety check to prevent infinite loops
        if (pos >= body.length()) {
            break;
        }
    }

    return file_found;
}

bool FileUploadHandler::process_part(Connection* conn, const std::string& body,
                                     const std::string& full_boundary,
                                     const std::string& end_boundary,
                                     size_t& pos, bool& file_found) {
    // Extract headers
    size_t headers_end;
    std::string headers;

    if (!extract_part_headers(body, pos, headers_end, headers)) {
        ErrorHandler::generate_error_response(conn, codes::BAD_REQUEST);
        return false;
    }

    // Extract filename from headers
    std::string filename;
    if (!extract_filename(headers, filename)) {
        // Not a file part, skip it
        // Find the end of this part to update pos for next iteration
        size_t content_end = body.find(full_boundary, pos);
        if (content_end != std::string::npos) {
            pos = content_end;
        } else {
            // Try end boundary
            content_end = body.find(end_boundary, pos);
            if (content_end != std::string::npos) {
                pos = content_end;
            } else {
                // Can't find next boundary, something's wrong
                ErrorHandler::generate_error_response(conn, codes::BAD_REQUEST);
                return false;
            }
        }
        return true;  // Skip this part but continue parsing
    }

    // Move to content start
    pos = headers_end + 4;  // Skip "\r\n\r\n"

    // Extract and process file content
    return extract_file_content(conn, body, pos, pos, full_boundary,
                                end_boundary, filename, file_found);
}

bool FileUploadHandler::extract_part_headers(const std::string& body,
                                             size_t& pos, size_t& headers_end,
                                             std::string& headers) {
    // Find the end of the headers section (double newline)
    headers_end = body.find("\r\n\r\n", pos);
    if (headers_end == std::string::npos) {
        return false;  // Invalid format
    }

    // Extract headers
    headers = body.substr(pos, headers_end - pos);
    return true;
}

bool FileUploadHandler::extract_filename(const std::string& headers,
                                         std::string& filename) {
    // Find Content-Disposition header
    size_t content_disp_pos = headers.find("Content-Disposition:");
    if (content_disp_pos == std::string::npos) {
        return false;  // Not a content disposition part
    }

    // Extract filename
    size_t filename_pos = headers.find("filename=\"", content_disp_pos);
    if (filename_pos == std::string::npos) {
        return false;  // No filename, not a file upload
    }

    filename_pos += 10;  // Length of 'filename="'
    size_t filename_end = headers.find("\"", filename_pos);
    if (filename_end == std::string::npos) {
        return false;  // Invalid format
    }

    filename = headers.substr(filename_pos, filename_end - filename_pos);
    return !filename
                .empty();  // Return true only if we found a non-empty filename
}

bool FileUploadHandler::extract_file_content(
    Connection* conn, const std::string& body, size_t pos, size_t& content_end,
    const std::string& full_boundary, const std::string& end_boundary,
    const std::string& filename, bool& file_found) {
    // Find next boundary
    content_end = body.find(full_boundary, pos);
    if (content_end == std::string::npos) {
        content_end = body.find(end_boundary, pos);
        if (content_end == std::string::npos) {
            ErrorHandler::generate_error_response(conn, codes::BAD_REQUEST);
            return false;
        }
    }

    // Bounds checking
    if (pos >= content_end || content_end > body.length()) {
        ErrorHandler::generate_error_response(conn, codes::BAD_REQUEST);
        return false;
    }

    // Remove trailing CRLF
    content_end -= 2;

    // Final validation after adjustment
    if (pos >= content_end) {
        ErrorHandler::generate_error_response(conn, codes::BAD_REQUEST);
        return false;
    }

    // Safe extraction
    std::vector<char> file_data(body.begin() + pos, body.begin() + content_end);

     if (!save_uploaded_file(conn, filename, file_data)) {
        return false;
    }

    file_found = true;
    return true;
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

bool FileUploadHandler::save_uploaded_file(Connection* conn,
                                           const std::string& filename,
                                           const std::vector<char>& data) {
    std::string upload_dir = get_upload_directory(conn);
    
    if (!ensure_upload_directory_exists(conn, upload_dir)) {
        return false;
    }

    std::string safe_filename = sanitize_filename(filename);
    if (safe_filename.empty()) {
        ErrorHandler::generate_error_response(conn, codes::BAD_REQUEST);

        return false;
    }

    std::string full_path = upload_dir + safe_filename;

    return write_file_to_disk(conn, full_path, data);
}

bool FileUploadHandler::ensure_upload_directory_exists(Connection* conn, 
                                                       const std::string& upload_dir) {
    struct stat st;
    if (stat(upload_dir.c_str(), &st) == 0) {
        return true;  // Directory already exists
    }

    // Directory doesn't exist, create it recursively
    return create_directory_recursive(conn, upload_dir);
}

bool FileUploadHandler::create_directory_recursive(Connection* conn, const std::string& path) {
    size_t pos = 0;
    while ((pos = path.find('/', pos + 1)) != std::string::npos) {
        if (pos > 0) {
            std::string parent_dir = path.substr(0, pos);
            if (mkdir(parent_dir.c_str(), 0755) != 0 && errno != EEXIST) {
                if (errno == EACCES || errno == EPERM) {
                    ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
                } else {
                    ErrorHandler::generate_error_response(conn, codes::INTERNAL_SERVER_ERROR);
                }
                return false;
            }
        }
    }

    // Create final directory
    if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
        if (errno == EACCES || errno == EPERM) {
            ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
        } else {
            ErrorHandler::generate_error_response(conn, codes::INTERNAL_SERVER_ERROR);
        }
        return false;
    }

    return true;
}

bool FileUploadHandler::write_file_to_disk(Connection* conn, const std::string& file_path,
                                           const std::vector<char>& data) {
    if (data.empty()) {
        ErrorHandler::generate_error_response(conn, codes::BAD_REQUEST);
        return false;
    }

    std::ofstream file(file_path.c_str(), std::ios::binary);

    if (!file.is_open()) {
        if (errno == EACCES || errno == EPERM) {
            ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
        } else if (errno == ENOSPC) {
            ErrorHandler::generate_error_response(conn, codes::INSUFFICIENT_STORAGE);
        } else {
            ErrorHandler::generate_error_response(conn, codes::INTERNAL_SERVER_ERROR);
        }
        return false;
    }

    // Write data
    file.write(&data[0], data.size());

    // Check for write errors
    if (file.fail() || file.bad()) {
        file.close();
        // Try to remove partially written file
        std::remove(file_path.c_str());

        if (errno == ENOSPC) {
            ErrorHandler::generate_error_response(conn, codes::INSUFFICIENT_STORAGE);
        } else {
            ErrorHandler::generate_error_response(conn, codes::INTERNAL_SERVER_ERROR);
        }
        return false;
    }

    file.close();
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

std::string FileUploadHandler::extract_boundary(
    const std::string& content_type) {
    size_t boundary_pos = content_type.find("boundary=");
    if (boundary_pos == std::string::npos) {
        return "";
    }

    boundary_pos += 9;  // Length of "boundary="

    // Add bounds checking
    if (boundary_pos >= content_type.length()) {
        return "";
    }

    // Check if boundary is quoted
    if (content_type[boundary_pos] == '"') {
        boundary_pos++;  // Skip opening quote
        size_t end_quote = content_type.find("\"", boundary_pos);
        if (end_quote == std::string::npos) {
            return "";
        }
        return content_type.substr(boundary_pos, end_quote - boundary_pos);
    } else {
        // Unquoted boundary ends at semicolon or end of string
        size_t end_pos = content_type.find(";", boundary_pos);
        if (end_pos == std::string::npos) {
            end_pos = content_type.length();
        }
        return content_type.substr(boundary_pos, end_pos - boundary_pos);
    }
}
