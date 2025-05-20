#include "webserv.hpp"

bool AHandler::process_location_redirect(Connection* conn) {
    const Location* location = conn->request_data_->location_match_;

    // Check if this location has a redirect
    if (location->redirect_.empty()) {
        return false;  // No redirect
    }

    std::cout << "\n==== REDIRECT ====\n";
    std::cout << "Redirecting to: " << location->redirect_ << std::endl;
    std::cout << "=================\n" << std::endl;

    // Set up redirect response
    conn->response_data_->status_code_ = 301;  // Moved Permanently
    conn->response_data_->status_message_ = "Moved Permanently";
    conn->response_data_->headers_["Location"] = location->redirect_;
    // conn->state_ = Connection::CONN_WRITING;

    return true;  // Redirect was processed
}

std::string AHandler::parse_absolute_path(HttpRequest* req) {
    // Extract request data
    const std::string& request_path = req->uri_;
    const Location* request_location = req->location_match_;
    std::string request_root = request_location->root_;

    // --CHECK If the root starts with /, removed it
    if (request_root[0] == '/') {
        request_root = request_root.substr(1);
    }

    std::cout << "\n==== STATIC FILE HANDLER ====\n";
    std::cout << "Request URI: " << request_path << std::endl;
    std::cout << "Matched location: " << request_location->path_ << std::endl;
    std::cout << "Root: " << request_location->root_ << std::endl;

    // Calculate the path relative to the location
    std::string relative_path = "";

    // Calculate where the relative part starts
    size_t location_len = request_location->path_.length();

    // If location path ends with /, exclude it from length calculation
    if (!request_location->path_.empty() &&
        request_location->path_[location_len - 1] == '/') {
        location_len--;
    }

    // Extract the relative part (starting after the location path)
    if (request_path.length() > location_len) {
        relative_path = request_path.substr(location_len + 1);
        if (relative_path[0] != '/') {
            relative_path = "/" + relative_path;
        }
    }

    std::string absolute_path;

    absolute_path = request_root + relative_path;

    std::cout << "Relative path: " << relative_path << std::endl;
    std::cout << "Absolute path: " << absolute_path << std::endl;
    std::cout << "============================\n" << std::endl;

    return (absolute_path);
}