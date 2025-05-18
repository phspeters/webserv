#include "webserv.hpp"

// curl -v -F "file=@files/cutecat.png" http://localhost:8080/upload

FileUploadHandler::FileUploadHandler(const ServerConfig& config)
    : AHandler(), config_(config) {}

FileUploadHandler::~FileUploadHandler() {}

void FileUploadHandler::handle(Connection* conn) {
    HttpRequest* req = conn->request_data_;
    HttpResponse* resp = conn->response_data_;

    if (!req || !resp) {
        ErrorHandler::internal_server_error(resp, config_);
        return;
    }

    std::string content_type = req->get_header("content-type");
    
    // Check that it's multipart/form-data
    if (content_type.empty() || content_type.find("multipart/form-data") != 0) {
        handle_upload_error(conn, UPLOAD_UNSUPPORTED_MEDIA);
        return;
    }

    // Extract boundary
    std::string boundary = extractBoundary(content_type);
    if (boundary.empty()) {
        handle_upload_error(conn, UPLOAD_BAD_REQUEST);
        return;
    }

    // Verify location_match_ is set by the router
    if (!req->location_match_) {
        ErrorHandler::internal_server_error(resp, config_);
        return;
    }

    // Process the multipart form data
    if (parseMultipartFormData(conn, boundary)) {
        // Success response
        resp->status_code_ = 201;
        resp->status_message_ = "Created";
        resp->content_type_ = "text/html";
        std::string body =
            "<html><body><h1>Upload Successful</h1><p>Your file has been uploaded successfully.</p></body></html>";
        resp->body_.assign(body.begin(), body.end());
        resp->content_length_ = resp->body_.size();
    } else {
        // Error response - specific error should have been set already
        // Default error if not set
        if (resp->status_code_ == 0) {
            handle_upload_error(conn, UPLOAD_BAD_REQUEST);
        }
    }
}

void FileUploadHandler::handle_upload_error(Connection* conn, UploadError error) {
    if (!conn || !conn->response_data_) {
        return;
    }
    
    switch (error) {
        case UPLOAD_BAD_REQUEST:
            ErrorHandler::bad_request(conn->response_data_, config_);
            break;
            
        case UPLOAD_UNSUPPORTED_MEDIA:
            ErrorHandler::unsupported_media_type(conn->response_data_, config_);
            break;
            
        case UPLOAD_PAYLOAD_TOO_LARGE:
            ErrorHandler::payload_too_large(conn->response_data_, config_);
            break;
            
        case UPLOAD_SERVER_ERROR:
        default:
            ErrorHandler::internal_server_error(conn->response_data_, config_);
            break;
    }
}

bool FileUploadHandler::parseMultipartFormData(Connection* conn, const std::string& boundary) {
    HttpRequest* req = conn->request_data_;

    // Full boundary string in the content
    std::string full_boundary = "--" + boundary;
    std::string end_boundary = full_boundary + "--";

    // Convert body to string for easier processing
    std::string body(req->body_.begin(), req->body_.end());
    
    // Find all parts
    size_t pos = 0;
    bool file_found = false;

    while (true) {
        // Find the start of a part
        pos = body.find(full_boundary, pos);
        if (pos == std::string::npos) {
            break;  // No more parts
        }

        // Move past boundary
        pos += full_boundary.length();

        // Check if we're at the end
        if (pos + 2 <= body.length() && body.substr(pos, 2) == "--") {
            break;  // End boundary
        }
        
        // Process this part
        if (!processPart(conn, body, full_boundary, end_boundary, pos, file_found)) {
            return false; // Error occurred
        }
    }

    if (!file_found) {
        handle_upload_error(conn, UPLOAD_BAD_REQUEST);
        return false;
    }

    return true;
}

bool FileUploadHandler::processPart(Connection* conn, const std::string& body,
                                   const std::string& full_boundary, const std::string& end_boundary,
                                   size_t& pos, bool& file_found) {
    // Extract headers
    size_t headers_end;
    std::string headers;
    
    if (!extractPartHeaders(body, pos, headers_end, headers)) {
        handle_upload_error(conn, UPLOAD_BAD_REQUEST);
        return false;
    }
    
    // Extract filename from headers
    std::string filename;
    if (!extractFilename(headers, filename)) {
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
                handle_upload_error(conn, UPLOAD_BAD_REQUEST);
                return false;
            }
        }
        return true; // Skip this part but continue parsing
    }
    
    // Move to content start
    pos = headers_end + 4;  // Skip "\r\n\r\n"
    
    // Extract and process file content
    return extractFileContent(conn, body, pos, pos, full_boundary, end_boundary, filename, file_found);
}

bool FileUploadHandler::extractPartHeaders(const std::string& body, size_t& pos, 
                                         size_t& headers_end, std::string& headers) {
    // Find the end of the headers section (double newline)
    headers_end = body.find("\r\n\r\n", pos);
    if (headers_end == std::string::npos) {
        return false;  // Invalid format
    }

    // Extract headers
    headers = body.substr(pos, headers_end - pos);
    return true;
}

bool FileUploadHandler::extractFilename(const std::string& headers, std::string& filename) {
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
    return !filename.empty(); // Return true only if we found a non-empty filename
}

bool FileUploadHandler::extractFileContent(Connection* conn, const std::string& body, 
                                         size_t pos, size_t& content_end,
                                         const std::string& full_boundary, const std::string& end_boundary,
                                         const std::string& filename, bool& file_found) {
    HttpRequest* req = conn->request_data_;

    // Find the end of this part (next boundary)
    content_end = body.find(full_boundary, pos);
    if (content_end == std::string::npos) {
        content_end = body.find(end_boundary, pos);
        if (content_end == std::string::npos) {
            handle_upload_error(conn, UPLOAD_BAD_REQUEST);
            return false;  // Invalid format
        }
    }

    // Adjust for trailing \r\n
    if (content_end >= 2) {
        content_end -= 2;
    }

    // Extract file content
    std::vector<char> file_data(body.begin() + pos, body.begin() + content_end);
                                
    // Check if file size exceeds limit
    if (!validate_upload_size(file_data.size(), req->location_match_)) {
        handle_upload_error(conn, UPLOAD_PAYLOAD_TOO_LARGE);
        return false;
    }

    // Save the file
    UploadError save_error = UPLOAD_SUCCESS;
    if (!saveUploadedFile(req, filename, file_data, save_error)) {
        handle_upload_error(conn, save_error);
        return false;
    }

    file_found = true;
    return true;
}

bool FileUploadHandler::validate_upload_size(size_t size, const LocationConfig* location) {
    // First check location-specific limit if available
    if (location && location->client_max_body_size > 0) {
        return size <= location->client_max_body_size;
    }
    
    // Fall back to server-wide limit
    return size <= config_.client_max_body_size_;
}

std::string FileUploadHandler::getUploadDirectory(HttpRequest* req) {
    std::string base_path = parse_absolute_path(req);
    
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

bool FileUploadHandler::saveUploadedFile(HttpRequest* req,
                                       const std::string& filename,
                                       const std::vector<char>& data,
                                       UploadError& error) {
    // Get the upload directory using root directive
    std::string upload_dir = getUploadDirectory(req);
    
    // Create upload directory if it doesn't exist
    struct stat st;
    if (stat(upload_dir.c_str(), &st) != 0) {
        // Directory doesn't exist, try to create it (recursively)
        size_t pos = 0;
        while ((pos = upload_dir.find('/', pos + 1)) != std::string::npos) {
            if (pos > 0) {
                std::string parent_dir = upload_dir.substr(0, pos);
                mkdir(parent_dir.c_str(), 0755);  // Ignore errors for existing dirs
            }
        }
        
        // Create final directory
        if (mkdir(upload_dir.c_str(), 0755) != 0 && errno != EEXIST) {
            error = UPLOAD_SERVER_ERROR;
            return false;
        }
    }

    // Sanitize filename
    std::string safe_filename = sanitizeFilename(filename);
    if (safe_filename.empty()) {
        error = UPLOAD_BAD_REQUEST;
        return false;
    }

    // Open file for writing
    std::string full_path = upload_dir + safe_filename;
    std::ofstream file(full_path.c_str(), std::ios::binary);

    if (!file.is_open()) {
        error = UPLOAD_SERVER_ERROR;
        return false;
    }

    // Write data
    file.write(&data[0], data.size());

    // Check for errors
    if (file.bad()) {
        file.close();
        error = UPLOAD_SERVER_ERROR;
        return false;
    }

    file.close();
    return true;
}

std::string FileUploadHandler::sanitizeFilename(const std::string& filename) {
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

    return safe_filename;
}

std::string FileUploadHandler::extractBoundary(
    const std::string& content_type) {
    size_t boundary_pos = content_type.find("boundary=");
    if (boundary_pos == std::string::npos) {
        return "";
    }

    boundary_pos += 9;  // Length of "boundary="

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
