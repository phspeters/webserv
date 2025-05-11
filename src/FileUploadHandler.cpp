#include "webserv.hpp"

// curl -v -F "file=@files/cutecat.png" http://localhost:8080/upload

FileUploadHandler::FileUploadHandler(const ServerConfig& config)
    : config_(config) {}

FileUploadHandler::~FileUploadHandler() {}

void FileUploadHandler::handle(Connection* conn) {
    HttpRequest* req = conn->request_data_;
    HttpResponse* resp = conn->response_data_;

    if (!req || !resp) {
        return;
    }

    // Verify it's a POST request
    if (req->method_ != "POST") {
        resp->status_code_ = 405;
        resp->status_message_ = "Method Not Allowed";
        return;
    }

    // Check for Content-Type header
    std::string content_type;
    for (std::map<std::string, std::string>::const_iterator it =
             req->headers_.begin();
         it != req->headers_.end(); ++it) {
        std::string header_name = it->first;
        for (size_t i = 0; i < header_name.length(); i++) {
            header_name[i] = std::tolower(header_name[i]);
        }
        if (header_name == "content-type") {
            content_type = it->second;
            break;
        }
    }

    // Check that it's multipart/form-data
    if (content_type.find("multipart/form-data") != 0) {
        resp->status_code_ = 415;
        resp->status_message_ = "Unsupported Media Type";
        return;
    }

    // Extract boundary
    std::string boundary = extractBoundary(content_type);
    if (boundary.empty()) {
        resp->status_code_ = 400;
        resp->status_message_ = "Bad Request - Invalid Boundary";
        return;
    }

    // Find the upload directory from location config
    const LocationConfig* location = NULL;
    std::string path = req->uri_;
    size_t query_pos = path.find('?');
    if (query_pos != std::string::npos) {
        path = path.substr(0, query_pos);
    }

    size_t best_match_length = 0;
    for (size_t i = 0; i < config_.locations.size(); ++i) {
        const LocationConfig& loc = config_.locations[i];
        if (path.find(loc.path) == 0 && loc.path.length() > best_match_length) {
            location = &loc;
            best_match_length = loc.path.length();
        }
    }

    if (!location) {
        resp->status_code_ = 500;
        resp->status_message_ = "Internal Server Error - No Location";
        return;
    }

    // Process the multipart form data
    if (parseMultipartFormData(conn, boundary)) {
        // Success response
        resp->status_code_ = 201;
        resp->status_message_ = "Created";
        resp->content_type_ = "text/html";
        std::string body =
            "<html><body><h1>Upload Successful</h1></body></html>";
        resp->body_.assign(body.begin(), body.end());
        resp->content_length_ = resp->body_.size();
    } else {
        // Error response
        resp->status_code_ = 400;
        resp->status_message_ = "Bad Request - Invalid Form Data";
        resp->content_type_ = "text/html";
        std::string body = "<html><body><h1>Upload Failed</h1></body></html>";
        resp->body_.assign(body.begin(), body.end());
        resp->content_length_ = resp->body_.size();
    }

}

bool FileUploadHandler::parseMultipartFormData(Connection* conn,
                                               const std::string& boundary) {
    HttpRequest* req = conn->request_data_;

    // Full boundary string in the content
    std::string full_boundary = "--" + boundary;
    std::string end_boundary = "--" + boundary + "--";

    // Convert body to string for easier processing
    std::string body(req->body_.begin(), req->body_.end());

    // Find all parts
    size_t pos = 0;
    bool success = false;

    while (true) {
        // Find the start of a part
        pos = body.find(full_boundary, pos);
        if (pos == std::string::npos) {
            break;  // No more parts
        }

        // Move past boundary
        pos += full_boundary.length();

        // Check if we're at the end
        if (body.substr(pos, 2) == "--") {
            success = true;
            break;  // End boundary
        }

        // Find the end of the headers section (double newline)
        size_t headers_end = body.find("\r\n\r\n", pos);
        if (headers_end == std::string::npos) {
            return false;  // Invalid format
        }

        // Extract headers
        std::string headers = body.substr(pos, headers_end - pos);

        // Find Content-Disposition header
        size_t content_disp_pos = headers.find("Content-Disposition:");
        if (content_disp_pos == std::string::npos) {
            continue;  // Skip this part
        }

        // Extract filename
        std::string filename;
        size_t filename_pos = headers.find("filename=\"", content_disp_pos);
        if (filename_pos != std::string::npos) {
            filename_pos += 10;  // Length of 'filename="'
            size_t filename_end = headers.find("\"", filename_pos);
            if (filename_end != std::string::npos) {
                filename =
                    headers.substr(filename_pos, filename_end - filename_pos);
            }
        }

        // If no filename, it's not a file upload
        if (filename.empty()) {
            continue;
        }

        // Move to content start
        pos = headers_end + 4;  // Skip "\r\n\r\n"

        // Find the end of this part (next boundary)
        size_t content_end = body.find(full_boundary, pos);
        if (content_end == std::string::npos) {
            content_end = body.find(end_boundary, pos);
            if (content_end == std::string::npos) {
                return false;  // Invalid format
            }
        }

        // Adjust for trailing \r\n
        content_end -= 2;

        // Extract file content
        std::vector<char> file_data(body.begin() + pos,
                                    body.begin() + content_end);

        // Save the file
        if (!saveUploadedFile(filename, file_data, "/tmp/uploads/")) {
            return false;
        }

        // Move position to the end of this part
        pos = content_end;
        success = true;
    }

    return success;
}

bool FileUploadHandler::saveUploadedFile(const std::string& filename,
                                         const std::vector<char>& data,
                                         const std::string& upload_dir) {
    // Create upload directory if it doesn't exist
    struct stat st;
    if (stat(upload_dir.c_str(), &st) != 0) {
        // Directory doesn't exist, try to create it
        if (mkdir(upload_dir.c_str(), 0755) != 0) {
            return false;
        }
    }

    // Sanitize filename
    std::string safe_filename = sanitizeFilename(filename);

    // Open file for writing
    std::string full_path = upload_dir + safe_filename;
    std::ofstream file(full_path.c_str(), std::ios::binary);

    if (!file.is_open()) {
        return false;
    }

    // Write data
    file.write(&data[0], data.size());

    // Check for errors
    if (file.bad()) {
        file.close();
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
