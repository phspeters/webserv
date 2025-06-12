#include "webserv.hpp"

// StaticFileHandler Flow

// 1. Location Redirect Check
// Checks if the location has a configured redirect
// Returns 301 Moved Permanently if a redirect is configured

// 2. Method Validation
// Verifies the request uses the GET method
// Returns 405 Method Not Allowed for non-GET requests

// 3. Path Resolution
// Converts the URI to an absolute filesystem path
// Combines location root with the relative path portion

// 4. Directory Redirect Check
// Checks if the path is a directory but URI lacks trailing slash
// Returns 301 Moved Permanently when trailing slash is needed

// 5. Directory Index Processing
// For directories with trailing slashes, looks for index files
// Updates path to include index file if one exists
// Sets up autoindex flag if needed

// 6. Directory Listing Generation
// Generates HTML directory listing if autoindex is enabled
// Returns 403 Forbidden if autoindex is disabled

// 7. File Existence Check
// Attempts to open the file
// Returns 404 Not Found if file doesn't exist

// 8. File Permission Check
// Checks if file is readable
// Returns 403 Forbidden if access is denied

// 9. File Type Validation
// Verifies the file is a regular file (not a directory, device, etc.)
// Returns appropriate error if file type isn't supported

// 10. Content Type Determination
// Sets the MIME type based on file extension
// Defaults to application/octet-stream if type is unknown

// 11. File Reading
// Reads the file content into memory
// Handles large files appropriately

// 12. Response Generation
// Sets status code to 200 OK for successful requests
// Adds appropriate headers (Content-Type, Content-Length, etc.)
// Populates response body with file content

// 13. Error Handling
// Returns 500 Internal Server Error for system-level errors
// Ensures proper cleanup even during error conditions

StaticFileHandler::StaticFileHandler() {}

StaticFileHandler::~StaticFileHandler() {}

void StaticFileHandler::handle(Connection* conn) {
    
    log(LOG_DEBUG, "StaticFileHandler::handle called for client_fd %d",
        conn->client_fd_);
    //--CHECK Check if the request has errors to return an error response

    // Fluxogram 301 - when location has redirect
    if (process_location_redirect(conn)) {
        return;  // Redirect response was set up, we're done
    }

    // Fluxogram 405 - call error handler
    // Check if the request method is allowed (only supporting GET) // Fluxogram
    // --CHECK I don't think it is necessary because of route
    // if (conn->request_data_->method_ != "GET") {
    //     ErrorHandler::generate_error_response(conn, codes::METHOD_NOT_ALLOWED);
    //     conn->response_data_->headers_["Allow"] = "GET";
    //     log(LOG_DEBUG, "StaticFileHandler::handle: Method not allowed for client_fd %d",
    //         conn->client_fd_);
    //     return;
    // }

    std::string absolute_path = parse_absolute_path(conn);

    // TEMP - Carol
    // if absolute_path ends with a slash, add index at end
    // if (!absolute_path.empty() && absolute_path[absolute_path.size() - 1] == '/') {
    //     std::cout << "Location match index: " << conn->location_match_->index_ << std::endl;
    //     absolute_path += conn->location_match_->index_;
    //     std::cout << "New absolute path: " << absolute_path << std::endl;
    // }
    if (!absolute_path.empty() && absolute_path[absolute_path.size() - 1] == '/') {
        if (conn->location_match_) {  // Add this null check
            std::cout << "Location match index: " << conn->location_match_->index_ << std::endl;
            absolute_path += conn->location_match_->index_;
            std::cout << "New absolute path: " << absolute_path << std::endl;
        } else {
            log(LOG_ERROR, "StaticFileHandler::handle: location_match_ is NULL for client_fd %d",
                conn->client_fd_);
            ErrorHandler::generate_error_response(conn, codes::INTERNAL_SERVER_ERROR);
            return;
        }
    }

    // Fluxogram 301 - check if the request should be a directory
    if (process_directory_redirect(conn, absolute_path)) {
        log(LOG_DEBUG, "StaticFileHandler::handle: Directory redirect for client_fd %d",
            conn->client_fd_);
        return;  // Redirect was set up, we're done
    }

    bool need_autoindex = false;
    if (process_directory_index(conn, absolute_path, need_autoindex)) {
        if (need_autoindex) {
            generate_directory_listing(conn, absolute_path);
            log(LOG_DEBUG, "StaticFileHandler::handle: Autoindex generated for client_fd %d",
                conn->client_fd_);
            conn->conn_state_ = codes::CONN_WRITING;
            return;
        }
    }

    // Try to open the file
    int fd = open(absolute_path.c_str(), O_RDONLY);
    log(LOG_DEBUG, "StaticFileHandler: Trying to open file: %s", absolute_path.c_str());
    log(LOG_DEBUG, "StaticFileHandler: open() returned fd=%d, errno=%d (%s)", 
        fd, errno, (fd == -1) ? strerror(errno) : "success");
    if (fd == -1) {
        // Fluxogram 404 - request resource not found - call error handler
        if (errno == ENOENT) {
            // File not found
            ErrorHandler::generate_error_response(conn, codes::NOT_FOUND);
            log(LOG_DEBUG, "StaticFileHandler::handle: File not found for client_fd %d",
                conn->client_fd_);
            // Not in the Fluxogram, but possible 403 - call error handler
        } else if (errno == EACCES) {
            // Permission denied
            ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
            log(LOG_DEBUG, "StaticFileHandler::handle: Permission denied for client_fd %d",
                conn->client_fd_);
            // Not in the Fluxogram, but possible 500 - call error handler
        } else {
            // Other error
            std::cout << "1" << std::endl;
            ErrorHandler::generate_error_response(conn, codes::INTERNAL_SERVER_ERROR);
            log(LOG_DEBUG, "StaticFileHandler::handle: Internal server error for client_fd %d",
                conn->client_fd_);
        }
        return;
    }

    // Get file info
    // Not in the Fluxogram, but possible 500 - call error handler
    struct stat file_info;
    if (fstat(fd, &file_info) == -1) {
        close(fd);
        std::cout << "2" << std::endl;
        ErrorHandler::generate_error_response(conn,codes::INTERNAL_SERVER_ERROR);
        log(LOG_DEBUG, "StaticFileHandler::handle: fstat failed for client_fd %d",
            conn->client_fd_);
        return;
    }

    // Check if it's a regular file
    // Not in the Fluxogram, but possible 403 - call error handler
    if (!S_ISREG(file_info.st_mode)) {
        close(fd);
        ErrorHandler::generate_error_response(conn, codes::FORBIDDEN);
        log(LOG_DEBUG, "StaticFileHandler::handle: File is not a regular file for client_fd %d",
            conn->client_fd_);
        return;
    }

    // Determine content type
    std::string content_type = "application/octet-stream";  // Default type
    size_t dot_pos = absolute_path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        std::string extension = absolute_path.substr(dot_pos + 1);
        // Map common extensions to MIME types
        if (extension == "html" || extension == "htm") {
            content_type = "text/html";
        } else if (extension == "css") {
            content_type = "text/css";
        } else if (extension == "js") {
            content_type = "application/javascript";
        } else if (extension == "jpg" || extension == "jpeg") {
            content_type = "image/jpeg";
        } else if (extension == "png") {
            content_type = "image/png";
        } else if (extension == "gif") {
            content_type = "image/gif";
        } else if (extension == "txt") {
            content_type = "text/plain";
        }
    }

    // Read the file content
    std::vector<char> file_content(file_info.st_size);
    ssize_t bytes_read = read(fd, &file_content[0], file_info.st_size);
    close(fd);

    // Not in the Fluxogram, but possible 500 - call error handler
    if (bytes_read != file_info.st_size) {
        std::cout << "3" << std::endl;
        ErrorHandler::generate_error_response(conn, codes::INTERNAL_SERVER_ERROR);
        log(LOG_DEBUG, "StaticFileHandler::handle: Read error for client_fd %d",
            conn->client_fd_);
        return;
    }

    // Prepare response headers
    conn->response_data_->set_header("Content-Type", content_type);


    // Convert file size to string using ostringstream (C++98 compatible)
    std::ostringstream size_stream;
    size_stream << file_info.st_size;
    conn->response_data_->set_header("Content-Length", size_stream.str());


    // Prepare the response
    // Fluxogram 200
    conn->response_data_->status_code_ = 200;
    conn->response_data_->status_message_ = "OK";
    conn->response_data_->body_.assign(file_content.begin(),
                                       file_content.end());
    conn->conn_state_ = codes::CONN_WRITING;
    log(LOG_DEBUG, "StaticFileHandler::handle: File served successfully for client_fd %d",
        conn->client_fd_);
}
