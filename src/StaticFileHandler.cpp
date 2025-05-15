#include "webserv.hpp"

StaticFileHandler::StaticFileHandler(const ServerConfig& config)
    : config_(config) {}

StaticFileHandler::~StaticFileHandler() {}

void StaticFileHandler::handle(Connection* conn) {

    std::string absolute_path = parse_absolute_path(conn->request_data_);
    
    // ENDS SAME METHODS FOR ALL HANDLERS
    
    /* // Try to open the file --CHECK EPOLL
    int fd = open(absolute_path.c_str(), O_RDONLY);
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
    
    // Determine content type
    std::string content_type = "application/octet-stream"; // Default type
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
    conn->response_data_->body_.assign(file_content.begin(), file_content.end());
    
    // Update connection state
    conn->state_ = Connection::CONN_WRITING; */
}