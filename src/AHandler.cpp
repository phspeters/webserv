#include "webserv.hpp"

bool AHandler::process_redirect(Connection* conn) {
    if (!conn || !conn->request_data_ || !conn->response_data_) {
        return false;  // Invalid connection
    }

    const Location* location = conn->request_data_->location_match_;

    if (!location) {
        return false;  // No location match, can't check for redirect
    }

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
    conn->conn_state_ = codes::CONN_WRITING;

    return true;  // Redirect was processed
}

// TEMP
std::string AHandler::parse_absolute_path(HttpRequest* req) {
    (void)req;
    return "./var/bin/www";  // Placeholder implementation
}
