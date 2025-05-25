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
    
    //--CHECK Check if the request has errors to return an error response

    // Fluxogram 301 - when location has redirect
    if (process_location_redirect(conn)) {
        return;  // Redirect response was set up, we're done
    }

    // Fluxogram 405 - call error handler
    // Check if the request method is allowed (only supporting GET) // Fluxogram
    // --CHECK I don't think it is necessary because of route
    if (conn->request_data_->method_ != "GET") {
        conn->response_data_->status_code_ = 405;
        conn->response_data_->status_message_ = "Method Not Allowed";
        conn->response_data_->headers_["Allow"] = "GET";
        // conn->state_ = Connection::CONN_WRITING;
        return;
    }

    std::string absolute_path = parse_absolute_path(conn);

    // Fluxogram 301 - check if the request should be a directory
    if (process_directory_redirect(conn, absolute_path)) {
        return;  // Redirect was set up, we're done
    }

    bool need_autoindex = false;
    if (process_directory_index(conn, absolute_path, need_autoindex)) {
        if (need_autoindex) {
            generate_directory_listing(conn, absolute_path);
            return;
        }
    }

    // Try to open the file
    int fd = open(absolute_path.c_str(), O_RDONLY);
    if (fd == -1) {
        // Fluxogram 404 - request resource not found - call error handler
        if (errno == ENOENT) {
            // File not found
            conn->response_data_->status_code_ = 404;
            conn->response_data_->status_message_ = "Not Found";
            // Not in the Fluxogram, but possible 403 - call error handler
        } else if (errno == EACCES) {
            // Permission denied
            conn->response_data_->status_code_ = 403;
            conn->response_data_->status_message_ = "Forbidden";
            // Not in the Fluxogram, but possible 500 - call error handler
        } else {
            // Other error
            conn->response_data_->status_code_ = 500;
            conn->response_data_->status_message_ = "Internal Server Error";
        }
        // conn->state_ = Connection::CONN_WRITING;
        return;
    }

    // Get file info
    // Not in the Fluxogram, but possible 500 - call error handler
    struct stat file_info;
    if (fstat(fd, &file_info) == -1) {
        close(fd);
        conn->response_data_->status_code_ = 500;
        conn->response_data_->status_message_ = "Internal Server Error";
        // conn->state_ = Connection::CONN_WRITING;
        return;
    }

    // Check if it's a regular file
    // Not in the Fluxogram, but possible 403 - call error handler
    if (!S_ISREG(file_info.st_mode)) {
        close(fd);
        conn->response_data_->status_code_ = 403;
        conn->response_data_->status_message_ = "Forbidden";
        // conn->state_ = Connection::CONN_WRITING;
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
        conn->response_data_->status_code_ = 500;
        conn->response_data_->status_message_ = "Internal Server Error";
        // conn->state_ = Connection::CONN_WRITING;
        return;
    }

    // Prepare response headers
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = content_type;

    // Convert file size to string using ostringstream (C++98 compatible)
    std::ostringstream size_stream;
    size_stream << file_info.st_size;
    headers["Content-Length"] = size_stream.str();

    // Prepare the response
    // Fluxogram 200
    conn->response_data_->status_code_ = 200;
    conn->response_data_->status_message_ = "OK";
    conn->response_data_->headers_ = headers;
    conn->response_data_->body_.assign(file_content.begin(),
                                       file_content.end());

    // Update connection state
    // conn->state_ = Connection::CONN_WRITING;
}
