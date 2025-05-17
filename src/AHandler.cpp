#include "webserv.hpp"

bool AHandler::process_redirect(Connection* conn) {
    
    if (!conn || !conn->request_data_ || !conn->response_data_) {
        return false; // Invalid connection
    }

    const LocationConfig* location = conn->request_data_->location_match_;
    
    if (!location) {
        return false; // No location match, can't check for redirect
    }
    
    // Check if this location has a redirect
    if (location->redirect.empty()) {
        return false; // No redirect
    }
    
    std::cout << "\n==== REDIRECT ====\n";
    std::cout << "Redirecting to: " << location->redirect << std::endl;
    std::cout << "=================\n" << std::endl;
    
    // Set up redirect response
    conn->response_data_->status_code_ = 301; // Moved Permanently
    conn->response_data_->status_message_ = "Moved Permanently";
    conn->response_data_->headers_["Location"] = location->redirect;
    conn->state_ = Connection::CONN_WRITING;
    
    return true; // Redirect was processed
}