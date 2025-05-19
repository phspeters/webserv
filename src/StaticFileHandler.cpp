#include "webserv.hpp"

StaticFileHandler::StaticFileHandler() {}

StaticFileHandler::~StaticFileHandler() {}

void StaticFileHandler::handle(Connection* conn) {
    //--CHECK Check if the request has errors to return an error response

    // Fluxogram 301
    if (process_redirect(conn)) {
        return;  // Redirect response was set up, we're done
    }

    // Fluxogram 405 - call error handler
    // Check if the request method is allowed (only supporting GET) // Fluxogram
    // OK
    if (conn->request_data_->method_ != "GET") {
        conn->response_data_->status_code_ = 405;
        conn->response_data_->status_message_ = "Method Not Allowed";
        conn->response_data_->headers_["Allow"] = "GET";
        conn->conn_state_ = codes::CONN_WRITING;
        return;
    }

    std::string absolute_path = parse_absolute_path(conn->request_data_);
    std::string index = conn->request_data_->location_match_->index_;

    // Check if the request URI ends with a slash (indicating directory)
    bool uri_ends_with_slash =
        !conn->request_data_->uri_.empty() &&
        conn->request_data_->uri_[conn->request_data_->uri_.length() - 1] ==
            '/';

    // Check if the absolute path is a directory
    struct stat path_stat;
    if (stat(absolute_path.c_str(), &path_stat) == 0 &&
        S_ISDIR(path_stat.st_mode)) {
        // Path exists and is a directory

        // If URI doesn't end with slash, redirect to add the slash (nginx
        // behavior)
        if (!uri_ends_with_slash) {
            std::cout << "Path is a directory but URI doesn't end with slash, "
                         "redirecting\n";

            // Create redirect URL (same path + /)
            std::string redirect_url = conn->request_data_->uri_ + "/";

            // Add query string if present
            size_t query_pos = redirect_url.find('?');
            if (query_pos != std::string::npos) {
                // Move ? to after the added slash
                redirect_url.insert(query_pos, "/");
                redirect_url.erase(redirect_url.length() - 1);
            }

            // Set up 301 redirect
            conn->response_data_->status_code_ = 301;
            conn->response_data_->status_message_ = "Moved Permanently";
            conn->response_data_->headers_["Location"] = redirect_url;
            conn->conn_state_ = codes::CONN_WRITING;
            return;
        }

        // If URI ends with slash, append index file
        absolute_path += index;
    }

    // Try to open the file
    int fd = open(absolute_path.c_str(), O_RDONLY);
    if (fd == -1) {
        // Fluxogram 404 - request resource not found
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
        conn->conn_state_ = codes::CONN_WRITING;
        return;
    }

    // Get file info
    struct stat file_info;
    if (fstat(fd, &file_info) == -1) {
        close(fd);
        conn->response_data_->status_code_ = 500;
        conn->response_data_->status_message_ = "Internal Server Error";
        conn->conn_state_ = codes::CONN_WRITING;
        return;
    }

    // Check if it's a regular file
    if (!S_ISREG(file_info.st_mode)) {
        close(fd);
        conn->response_data_->status_code_ = 403;
        conn->response_data_->status_message_ = "Forbidden";
        conn->conn_state_ = codes::CONN_WRITING;
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

    if (bytes_read != file_info.st_size) {
        conn->response_data_->status_code_ = 500;
        conn->response_data_->status_message_ = "Internal Server Error";
        conn->conn_state_ = codes::CONN_WRITING;
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
    conn->response_data_->status_code_ = 200;
    conn->response_data_->status_message_ = "OK";
    conn->response_data_->headers_ = headers;
    conn->response_data_->body_.assign(file_content.begin(),
                                       file_content.end());

    // Update connection state
    conn->conn_state_ = codes::CONN_WRITING;
}
