#include "webserv.hpp"

ResponseWriter::ResponseWriter() {}

ResponseWriter::~ResponseWriter() {}

codes::WriteStatus ResponseWriter::write_response(Connection* conn) {
    log(LOG_DEBUG, "handle_write: Writing response to client_fd %d",
        conn->client_fd_);

    // Validate connection
    if (!conn || conn->client_fd_ < 0) {
        return codes::WRITING_ERROR;
    }

    // If buffer is empty, prepare the response data first
    if (conn->write_buffer_.empty()) {
        // Write headers
        if (!write_headers(conn)) {
            return codes::WRITING_ERROR;
        }

        // Write body
        if (!write_body(conn)) {
            return codes::WRITING_ERROR;
        }
    }

    // Nothing to send
    if (conn->write_buffer_.empty()) {
        return codes::WRITING_SUCCESS;
    }

    // Send the response
    ssize_t bytes_written =
        send(conn->client_fd_,
             conn->write_buffer_.data() + conn->write_buffer_offset_,
             conn->write_buffer_.size() - conn->write_buffer_offset_,
             MSG_NOSIGNAL);  // Prevents SIGPIPE

    // Check for errors (any non positive return is an error)
    if (bytes_written <= 0) {
        // With level-triggered epoll, if we're here, it's a real error
        // No need to check errno specifically
        return codes::WRITING_ERROR;
    }

    // Update the offset instead of erasing
    conn->write_buffer_offset_ += bytes_written;

    // Update the last activity timestamp
    conn->last_activity_ = time(NULL);

    // Check if we've written everything
    if (conn->write_buffer_offset_ == conn->write_buffer_.size()) {
        return codes::WRITING_SUCCESS;
    }

    // More data to send
    return codes::WRITING_INCOMPLETE;
}

bool ResponseWriter::write_headers(Connection* conn) {
    if (!conn) {
        return false;
    }

    HttpResponse* resp = conn->response_data_;

    // Format the response status line and headers
    std::stringstream headers;

    headers << resp->version_ << " " << resp->status_code_ << " "
            << get_status_message(resp->status_code_) << "\r\n";

    // Add custom headers first
    for (std::map<std::string, std::string>::const_iterator it =
             resp->headers_.begin();
         it != resp->headers_.end(); ++it) {
        headers << it->first << ": " << it->second << "\r\n";
    }

    // Add default headers if not already set
    if (resp->headers_.find("date") == resp->headers_.end()) {
        headers << "Date: " << get_current_gmt_time() << "\r\n";
    }

    if (resp->headers_.find("server") == resp->headers_.end()) {
        headers << "Server: Webserv/1.0\r\n";
    }

    if (resp->headers_.find("content-type") == resp->headers_.end() &&
        !resp->content_type_.empty()) {
        headers << "Content-Type: " << resp->content_type_ << "\r\n";
    }

    if (resp->headers_.find("content-length") == resp->headers_.end()) {
        headers << "Content-Length: " << resp->content_length_ << "\r\n";
    }

    // End headers section
    headers << "\r\n";

    // Convert to string and add to write buffer
    std::string headers_str = headers.str();
    conn->write_buffer_.insert(conn->write_buffer_.end(), headers_str.begin(),
                               headers_str.end());

    return true;
}

bool ResponseWriter::write_body(Connection* conn) {
    if (!conn || !conn->response_data_) {
        return false;
    }

    // Add body content to write buffer if it exists
    if (!conn->response_data_->body_.empty()) {
        // Insert body content into write buffer (vector<char>)
        conn->write_buffer_.insert(conn->write_buffer_.end(), 
                                  conn->response_data_->body_.begin(),
                                  conn->response_data_->body_.end());
        
        log(LOG_DEBUG, "Added %zu bytes of body content to write buffer for client_fd %d", 
            conn->response_data_->body_.size(), conn->client_fd_);
    } else {
        log(LOG_WARNING, "Response body is empty for client_fd %d", conn->client_fd_);
    }
    
    return true;
}

std::string ResponseWriter::get_current_gmt_time() const {
    char buffer[100];
    time_t now = time(NULL);
    struct tm* tm_info = gmtime(&now);

    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    return std::string(buffer);
}
