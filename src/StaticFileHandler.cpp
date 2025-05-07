#include "webserv.hpp"

StaticFileHandler::StaticFileHandler(const ServerConfig& config, ResponseWriter& writer) : config_(config), response_writer_(writer) {
}

StaticFileHandler::~StaticFileHandler() {}

void StaticFileHandler::handle(Connection* conn) {

    // Extract the request path
    const std::string& request_path = conn->request_data_->path_;
    
    // Debug information
    std::cout << "\n==== STATIC FILE HANDLER ====\n";
    std::cout << "Handling request for path: " << request_path << std::endl;
    
    // Find the matching location to determine the file path
    const LocationConfig* loc = NULL;
    for (std::vector<LocationConfig>::const_iterator it = config_.locations.begin(); 
         it != config_.locations.end(); ++it) {
        if (request_path.find(it->path) == 0) {
            if (it->path == "/" || 
                request_path == it->path || 
                (request_path.length() > it->path.length() && request_path[it->path.length()] == '/')) {
                if (!loc || it->path.length() > loc->path.length()) {
                    loc = &(*it);
                }
            }
        }
    }
    
    if (!loc) {
        std::cout << "No matching location found\n";
        std::cout << "============================\n" << std::endl;
        conn->response_data_->status_code_ = 404;
        conn->response_data_->status_message_ = "Not Found";
        conn->state_ = Connection::CONN_WRITING;;
        return;
    }
    
    // Build the file path
    std::string file_path = loc->root;
    if (!file_path.empty() && file_path[file_path.size() - 1] != '/') {
        file_path += '/';
    }
    
    // Translate URL path to file path
    std::string relative_path = request_path;
    if (loc->path != "/") {
        // Remove the location prefix from the path
        relative_path = request_path.substr(loc->path.length());
    }
    
    // If path ends with /, look for index file
    if (relative_path.empty() || 
    (!relative_path.empty() && relative_path[relative_path.size() - 1] == '/')) {
    relative_path += loc->index;
    }   
    
    // Remove leading / from relative_path if present
    if (!relative_path.empty() && relative_path[0] == '/') {
        relative_path = relative_path.substr(1);
    }
    
    file_path += relative_path;
    
    std::cout << "File path: " << file_path << std::endl;
    std::cout << "============================\n" << std::endl;
    
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
        conn->state_ = Connection::CONN_WRITING;;
        return;
    }
    
    // Get file info
    struct stat file_info;
    if (fstat(fd, &file_info) == -1) {
        close(fd);
        conn->response_data_->status_code_ = 500;
        conn->response_data_->status_message_ = "Internal Server Error";
        conn->state_ = Connection::CONN_WRITING;;
        return;
    }
    
    // Check if it's a regular file
    if (!S_ISREG(file_info.st_mode)) {
        close(fd);
        conn->response_data_->status_code_ = 403;
        conn->response_data_->status_message_ = "Forbidden";
        conn->state_ = Connection::CONN_WRITING;;
        return;
    }
    
    // Determine content type
    std::string content_type = "application/octet-stream"; // Default type
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
    ssize_t bytes_read = read(fd, file_content.data(), file_info.st_size);
    close(fd);
    
    if (bytes_read != file_info.st_size) {
        conn->response_data_->status_code_ = 500;
        conn->response_data_->status_message_ = "Internal Server Error";
        conn->state_ = Connection::CONN_WRITING;;
        return;
    }
    
   // Prepare response headers
std::map<std::string, std::string> headers;
headers["Content-Type"] = content_type;

    // Use stringstream instead of std::to_string (C++11 feature)
    std::ostringstream ss;
    ss << file_info.st_size;
    headers["Content-Length"] = ss.str();

    // Send the response
    conn->response_data_->status_code_ = 200;
    conn->response_data_->status_message_ = "OK";
    conn->response_data_->headers_ = headers;
    conn->response_data_->body_.assign(file_content.begin(), file_content.end());
    
    // Update connection state
    conn->state_ = Connection::CONN_WRITING;;
}