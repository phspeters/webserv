#include "webserv.hpp"

StaticFileHandler::StaticFileHandler(const ServerConfig& config)
    : config_(config) {}

StaticFileHandler::~StaticFileHandler() {}

void StaticFileHandler::handle(Connection* conn) {
    // Extract the request path
    const std::string& request_path = conn->request_data_->uri_;

    // Debug information
    std::cout << "\n==== STATIC FILE HANDLER ====\n";
    std::cout << "Handling request for path: " << request_path << std::endl;

    // Pointer to matching location in the config
    const LocationConfig* loc =
        NULL;  // Pointer to the matching location --CHECK

    // Get the root and location path from the matched location
    std::string root = loc->root;
    std::string location_path = loc->path;

    // Ensure root ends with / --CHECK IF NEEDS
    if (!root.empty() && root[root.size() - 1] != '/') {
        root += '/';
    }

    // Calculate the relative path
    std::string relative_path;
    if (location_path == "/") {
        // If location is root, use the entire request path (minus the leading
        // slash)
        if (!request_path.empty() && request_path[0] == '/') {
            relative_path = request_path.substr(1);
        } else {
            relative_path = request_path;
        }
    } else {
        // Remove the location prefix to get the file path relative to the root
        // Example: location /blog with request /blog/post.html -> relative_path
        // = post.html
        if (request_path.find(location_path) == 0) {
            relative_path = request_path.substr(location_path.length());
            // Remove leading slash if present
            if (!relative_path.empty() && relative_path[0] == '/') {
                relative_path = relative_path.substr(1);
            }
        } else {
            // Fallback (shouldn't happen with proper routing)
            if (!request_path.empty() && request_path[0] == '/') {
                relative_path = request_path.substr(1);
            } else {
                relative_path = request_path;
            }
        }
    }

    // If the path is empty or ends with /, append the index file
    if (relative_path.empty() ||
        (!relative_path.empty() &&
         relative_path[relative_path.size() - 1] == '/')) {
        if (!loc->index.empty()) {
            relative_path += loc->index;
        } else {
            relative_path += "index.html";  // Default index if not specified
        }
    }

    // Combine root and relative path to get the complete file path
    std::string file_path = root + relative_path;

    std::cout << "Root: " << root << std::endl;
    std::cout << "Relative path: " << relative_path << std::endl;
    std::cout << "Complete file path: " << file_path << std::endl;
    std::cout << "============================\n" << std::endl;

    // Check if method is allowed (if allowed_methods is specified)
    if (!loc->allowed_methods.empty()) {
        bool method_allowed = false;
        for (std::vector<std::string>::const_iterator it =
                 loc->allowed_methods.begin();
             it != loc->allowed_methods.end(); ++it) {
            if (conn->request_data_->method_ == *it) {
                method_allowed = true;
                break;
            }
        }

        if (!method_allowed) {
            std::cout << "Method not allowed: " << conn->request_data_->method_
                      << std::endl;
            conn->response_data_->status_code_ = 405;
            conn->response_data_->status_message_ = "Method Not Allowed";
            conn->state_ = Connection::CONN_WRITING;
            return;
        }
    }

    // Try to open the file
    int fd = open(file_path.c_str(), O_RDONLY);
    if (fd == -1) {
        if (errno == ENOENT) {
            // File not found
            conn->response_data_->status_code_ = 404;
            conn->response_data_->status_message_ = "Not Found";
        } else if (errno == EACCES) {
            // Permission denied
            conn->response_data_->status_code_ = 403;
            conn->response_data_->status_message_ = "Forbidden";
        } else {
            // Other error
            conn->response_data_->status_code_ = 500;
            conn->response_data_->status_message_ = "Internal Server Error";
        }
        conn->state_ = Connection::CONN_WRITING;
        return;
    }

    // Get file info
    struct stat file_info;
    if (fstat(fd, &file_info) == -1) {
        close(fd);
        conn->response_data_->status_code_ = 500;
        conn->response_data_->status_message_ = "Internal Server Error";
        conn->state_ = Connection::CONN_WRITING;
        return;
    }

    // Check if it's a regular file
    if (!S_ISREG(file_info.st_mode)) {
        close(fd);
        conn->response_data_->status_code_ = 403;
        conn->response_data_->status_message_ = "Forbidden";
        conn->state_ = Connection::CONN_WRITING;
        return;
    }

    // Determine content type --CHECK UNDERSTAND BETTER
    std::string content_type = "application/octet-stream";  // Default type
    size_t dot_pos = file_path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        std::string extension = file_path.substr(dot_pos + 1);

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

    if (bytes_read != file_info.st_size) {
        conn->response_data_->status_code_ = 500;
        conn->response_data_->status_message_ = "Internal Server Error";
        conn->state_ = Connection::CONN_WRITING;
        return;
    }

    // Prepare response headers
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = content_type;

    // Convert file size to string using ostringstream (C++98 compatible)
    std::ostringstream size_stream;
    size_stream << file_info.st_size;
    headers["Content-Length"] = size_stream.str();

    // Send the response
    conn->response_data_->status_code_ = 200;
    conn->response_data_->status_message_ = "OK";
    conn->response_data_->headers_ = headers;
    conn->response_data_->body_.assign(file_content.begin(),
                                       file_content.end());

    // Update connection state
    conn->state_ = Connection::CONN_WRITING;
}