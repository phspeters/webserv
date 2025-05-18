#include "webserv.hpp"

CgiHandler::CgiHandler() {}

CgiHandler::~CgiHandler() {}

void CgiHandler::handle(Connection* conn) {
    // Check if the request has errors

    // Check redirect
    if (process_redirect(conn)) {
        return;  // Redirect response was set up, we're done
    }

    // Extract request data
    const std::string& request_uri = conn->request_data_->uri_;
    const std::string& request_method = conn->request_data_->method_;
    const Location* location = conn->request_data_->location_match_;

    std::cout << "\n==== CGI HANDLER ====\n";
    std::cout << "Request URI: " << request_uri << std::endl;
    std::cout << "Request Method: " << request_method << std::endl;

    if (!location) {
        std::cout << "Error: No matching location found\n";
        std::cout << "=====================\n" << std::endl;
        conn->response_data_->status_code_ = 404;
        conn->response_data_->status_message_ = "Not Found";
        conn->conn_state_ = codes::CONN_WRITING;
        return;
    }

    std::cout << "Matched location: " << location->path_ << std::endl;
    std::cout << "Root: " << location->root_ << std::endl;

    // Check if method is allowed (typically GET or POST for CGI)
    if (request_method != "GET" && request_method != "POST") {
        std::cout << "Error: Method not allowed for CGI\n";
        std::cout << "=====================\n" << std::endl;
        conn->response_data_->status_code_ = 405;
        conn->response_data_->status_message_ = "Method Not Allowed";
        conn->response_data_->headers_["Allow"] = "GET, POST";
        conn->conn_state_ = codes::CONN_WRITING;
        return;
    }

    // Check if URI ends with a slash (which would indicate a directory)
    if (!request_uri.empty() && request_uri[request_uri.length() - 1] == '/') {
        std::cout
            << "Error: URI ends with slash, cannot execute directory as CGI\n";
        std::cout << "=====================\n" << std::endl;
        conn->response_data_->status_code_ = 400;
        conn->response_data_->status_message_ = "Bad Request";
        conn->conn_state_ = codes::CONN_WRITING;
        return;
    }

    // Parse the absolute path to the CGI script
    std::string script_path = parse_absolute_path(conn->request_data_);

    if (script_path.empty()) {
        std::cout << "Error: Failed to determine CGI script path\n";
        std::cout << "=====================\n" << std::endl;
        conn->response_data_->status_code_ = 500;
        conn->response_data_->status_message_ = "Internal Server Error";
        conn->conn_state_ = codes::CONN_WRITING;
        return;
    }

    // Check if the script exists and is executable
    struct stat file_info;
    if (stat(script_path.c_str(), &file_info) == -1) {
        if (errno == ENOENT) {
            std::cout << "Error: CGI script not found\n";
            std::cout << "=====================\n" << std::endl;
            conn->response_data_->status_code_ = 404;
            conn->response_data_->status_message_ = "Not Found";
        } else {
            std::cout << "Error: Failed to access CGI script: "
                      << strerror(errno) << std::endl;
            std::cout << "=====================\n" << std::endl;
            conn->response_data_->status_code_ = 500;
            conn->response_data_->status_message_ = "Internal Server Error";
        }
        conn->conn_state_ = codes::CONN_WRITING;
        return;
    }

    // Check if it's a regular file
    if (!S_ISREG(file_info.st_mode)) {
        std::cout << "Error: CGI script is not a regular file\n";
        std::cout << "=====================\n" << std::endl;
        conn->response_data_->status_code_ = 403;
        conn->response_data_->status_message_ = "Forbidden";
        conn->conn_state_ = codes::CONN_WRITING;
        return;
    }

    // Check if the file is executable
    if (!(file_info.st_mode & S_IXUSR)) {
        std::cout << "Error: CGI script is not executable\n";
        std::cout << "=====================\n" << std::endl;
        conn->response_data_->status_code_ = 403;
        conn->response_data_->status_message_ = "Forbidden";
        conn->conn_state_ = codes::CONN_WRITING;
        return;
    }

    // TODO: Execute the CGI script
    // 1. Setup environment variables
    // 2. Create pipes for communication
    // 3. Fork a process
    // 4. Execute the script
    // 5. Read the output
    // 6. Parse headers from the output
    // 7. Set response based on script output

    // For now, return a "Not Implemented" response
    std::cout << "TODO: Execute CGI script (Not yet implemented)\n";
    std::cout << "=====================\n" << std::endl;

    conn->response_data_->status_code_ = 501;
    conn->response_data_->status_message_ = "Not Implemented";
    conn->response_data_->headers_["Content-Type"] = "text/plain";
    std::string body = "CGI execution not yet implemented";
    conn->response_data_->body_.assign(body.begin(), body.end());
    conn->conn_state_ = codes::CONN_WRITING;
}

// First method: Extract the script information from the request
bool CgiHandler::extract_script_info(HttpRequest* req) {
    if (!req || !req->location_match_) {
        return false;
    }

    const std::string& request_path = req->uri_;
    const Location* request_location = req->location_match_;

    // Calculate where the relative part starts
    size_t location_len = request_location->path_.length();

    // If location path ends with /, exclude it from length calculation
    if (!request_location->path_.empty() &&
        request_location->path_[location_len - 1] == '/') {
        location_len--;
    }

    // Get the part of the URL after the location path
    std::string relative_url = "";
    if (request_path.length() > location_len) {
        if (request_path[location_len] == '/') {
            relative_url = request_path.substr(location_len + 1);
        } else {
            relative_url = request_path.substr(location_len);
        }
    }

    // If relative URL is empty, we can't execute a CGI script
    if (relative_url.empty()) {
        return false;
    }

    // For CGI, we need to determine the actual script and the PATH_INFO
    // First, find the script name (up to the first '/' or the end)
    size_t path_separator = relative_url.find('/');

    if (path_separator != std::string::npos) {
        // Extract script path and path info
        script_path_ = relative_url.substr(0, path_separator);
        path_info_ = relative_url.substr(path_separator);
    } else {
        // No path separator, whole relative URL is the script
        script_path_ = relative_url;
        path_info_ = "";
    }

    // Extract query string if present
    size_t query_pos = request_path.find('?');
    if (query_pos != std::string::npos) {
        query_string_ = request_path.substr(query_pos + 1);
    } else {
        query_string_ = "";
    }

    return true;
}

// Second method: Build the absolute path from root and script path
std::string CgiHandler::parse_absolute_path(HttpRequest* req) {
    if (!req || !req->location_match_) {
        return "";
    }

    // First extract script information
    if (!extract_script_info(req)) {
        return "";
    }

    std::string request_root = req->location_match_->root_;

    // --CHECK If the root starts with /, remove it
    if (!request_root.empty() && request_root[0] == '/') {
        request_root = request_root.substr(1);
    }

    // Ensure root path ends with / if not empty
    if (!request_root.empty() && request_root[request_root.size() - 1] != '/') {
        request_root += '/';
    }

    // Combine root and script path to get the absolute path
    std::string absolute_path = request_root + script_path_;

    std::cout << "CGI Script details:\n";
    std::cout << "  Script path: " << script_path_ << std::endl;
    std::cout << "  Path info: " << path_info_ << std::endl;
    std::cout << "  Query string: " << query_string_ << std::endl;
    std::cout << "  Absolute path: " << absolute_path << std::endl;

    return absolute_path;
}