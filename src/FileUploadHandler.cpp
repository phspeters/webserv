#include "webserv.hpp"

// curl -v -F "file=@files/cutecat.png" http://localhost:8080/upload

FileUploadHandler::FileUploadHandler() : AHandler() {}

FileUploadHandler::~FileUploadHandler() {}

void FileUploadHandler::handle(Connection* conn) {
    HttpRequest* req = conn->request_data_;
    HttpResponse* resp = conn->response_data_;

    if (!req || !resp) {
        return;
    }

    std::string content_type = req->get_header("content-type");

    // Check that it's multipart/form-data
    if (content_type.empty() || content_type.find("multipart/form-data") != 0) {
        resp->status_code_ = 415;
        resp->status_message_ = "Unsupported Media Type";
        return;
    }

    // Extract boundary
    std::string boundary = extract_boundary(content_type);
    if (boundary.empty()) {
        resp->status_code_ = 400;
        resp->status_message_ = "Bad Request - Invalid Boundary";
        return;
    }

    // Verify location_match_ is set by the router
    if (!req->location_match_) {
        resp->status_code_ = 500;
        resp->status_message_ = "Internal Server Error - No Location Match";
        return;
    }

    // Process the multipart form data
    if (parse_multipart_form_data(conn, boundary)) {
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

bool FileUploadHandler::parse_multipart_form_data(Connection* conn,
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
        if (!save_uploaded_file(req, filename, file_data)) {
            return false;
        }

        // Move position to the end of this part
        pos = content_end;
        success = true;
    }

    return success;
}

std::string FileUploadHandler::get_upload_directory(HttpRequest* req) {
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

bool FileUploadHandler::save_uploaded_file(HttpRequest* req,
                                           const std::string& filename,
                                           const std::vector<char>& data) {
    // Get the upload directory using root directive
    std::string upload_dir = get_upload_directory(req);

    // Create upload directory if it doesn't exist
    struct stat st;
    if (stat(upload_dir.c_str(), &st) != 0) {
        // Directory doesn't exist, try to create it (recursively)
        size_t pos = 0;
        while ((pos = upload_dir.find('/', pos + 1)) != std::string::npos) {
            if (pos > 0) {
                std::string parent_dir = upload_dir.substr(0, pos);
                mkdir(parent_dir.c_str(),
                      0755);  // Ignore errors for existing dirs
            }
        }

        // Create final directory
        if (mkdir(upload_dir.c_str(), 0755) != 0 && errno != EEXIST) {
            return false;
        }
    }

    // Sanitize filename
    std::string safe_filename = sanitize_filename(filename);
    if (safe_filename.empty()) {
        return false;
    }

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

    return safe_filename;
}

std::string FileUploadHandler::extract_boundary(
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
