#include "webserv.hpp"

CgiHandler::CgiHandler() {}

CgiHandler::~CgiHandler() {}

void CgiHandler::handle(Connection* conn) {
    
    // 1. Check redirect (same as StaticFileHandler)
    if (process_location_redirect(conn)) {
        return;  // Redirect response was set up, we're done
    }

    // Extract request data
    const std::string& request_uri = conn->request_data_->uri_;
    const std::string& request_method = conn->request_data_->method_;
    const Location* location = conn->request_data_->location_match_;

    std::cout << "\n==== CGI HANDLER ====\n";
    std::cout << "Request URI: " << request_uri << std::endl;
    std::cout << "Request Method: " << request_method << std::endl;
    std::cout << "Matched location: " << location->path_ << std::endl;
    std::cout << "Root: " << location->root_ << std::endl;

    // 2. Method Validation (GET or POST only)
    if (request_method != "GET" && request_method != "POST") {
        conn->response_data_->status_code_ = 405;
        conn->response_data_->status_message_ = "Method Not Allowed";
        conn->response_data_->headers_["Allow"] = "GET, POST";
        return;
    }

    // 3. URI Format Check (can't execute a directory)
    if (!request_uri.empty() && request_uri[request_uri.length() - 1] == '/') {
        std::cout << "Error: URI ends with slash, cannot execute directory as CGI\n";
        std::cout << "=====================\n" << std::endl;
        conn->response_data_->status_code_ = 400;
        conn->response_data_->status_message_ = "Bad Request";
        return;
    }

    // 4. Script Path Resolution
    std::string script_path = parse_absolute_path(conn->request_data_);
    // --CHECK I dont think this is necessary 
    // if (script_path.empty()) {
    //     std::cout << "Error: Failed to determine CGI script path\n";
    //     std::cout << "=====================\n" << std::endl;
    //     conn->response_data_->status_code_ = 500;
    //     conn->response_data_->status_message_ = "Internal Server Error";
    //     return;
    // }

    // 5. Script Extension Check
    bool is_valid_extension = false;
    std::string extension;
    size_t dot_pos = script_path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        extension = script_path.substr(dot_pos + 1);
        // --CHECK - Check against allowed extensions
        if (extension == "php" || extension == "py" || extension == "sh") {
            is_valid_extension = true;
        }
    }

    if (!is_valid_extension) {
        std::cout << "Error: Invalid CGI script extension: " << extension << std::endl;
        std::cout << "=====================\n" << std::endl;
        conn->response_data_->status_code_ = 403;
        conn->response_data_->status_message_ = "Forbidden";
        return;
    }

    // 6. Script Existence Check
    struct stat file_info;
    if (stat(script_path.c_str(), &file_info) == -1) {
        if (errno == ENOENT) {
            std::cout << "Error: CGI script not found\n";
            std::cout << "=====================\n" << std::endl;
            conn->response_data_->status_code_ = 404;
            conn->response_data_->status_message_ = "Not Found";
        } else {
            std::cout << "Error: Failed to access CGI script: " << strerror(errno) << std::endl;
            std::cout << "=====================\n" << std::endl;
            conn->response_data_->status_code_ = 500;
            conn->response_data_->status_message_ = "Internal Server Error";
        }
        return;
    }

    // 7. Script Type Validation
    if (!S_ISREG(file_info.st_mode)) {
        std::cout << "Error: CGI script is not a regular file\n";
        std::cout << "=====================\n" << std::endl;
        conn->response_data_->status_code_ = 403;
        conn->response_data_->status_message_ = "Forbidden";
        return;
    }

    // 8. Script Permission Check
    if (!(file_info.st_mode & S_IXUSR)) {
        std::cout << "Error: CGI script is not executable\n";
        std::cout << "=====================\n" << std::endl;
        conn->response_data_->status_code_ = 403;
        conn->response_data_->status_message_ = "Forbidden";
        return;
    }

    // 9. Content-Length Check for POST
    if (request_method == "POST") {
        // Check if Content-Length is present for POST requests
        if (conn->request_data_->headers_.find("Content-Length") == conn->request_data_->headers_.end()) {
            std::cout << "Error: Content-Length header missing for POST request\n";
            std::cout << "=====================\n" << std::endl;
            conn->response_data_->status_code_ = 411;
            conn->response_data_->status_message_ = "Length Required";
            return;
        }

        // --CHECK Max Length
        // Check if Content-Length exceeds max allowed size (e.g., 10MB)
        const size_t MAX_CONTENT_LENGTH = 10 * 1024 * 1024; // 10MB
        size_t content_length = 0;
        
        try {
            content_length = std::atoi(conn->request_data_->headers_["Content-Length"].c_str());
        } catch (...) {
            content_length = 0;
        }

        if (content_length > MAX_CONTENT_LENGTH) {
            std::cout << "Error: Request body too large: " << content_length << " bytes\n";
            std::cout << "=====================\n" << std::endl;
            conn->response_data_->status_code_ = 413;
            conn->response_data_->status_message_ = "Request Entity Too Large";
            return;
        }
    }

    // ---------- EXECUTE THE CGI SCRIPT ----------

    // ---------- END EXECUTE THE CGI SCRIPT ----------
    
}

