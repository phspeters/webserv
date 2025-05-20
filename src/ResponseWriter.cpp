#include "webserv.hpp"

ResponseWriter::ResponseWriter() {}

ResponseWriter::~ResponseWriter() {}

codes::WriterState ResponseWriter::write_response(Connection* conn) {
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
        return codes::WRITING_COMPLETE;
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
        return codes::WRITING_COMPLETE;
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
            << HttpResponse::get_status_message(resp->status_code_) << "\r\n";

    // Add Date header if not present
    if (resp->headers_.find("Date") == resp->headers_.end()) {
        headers << "Date: " << get_current_gmt_time() << "\r\n";
    }

    // Add Server header if not present
    if (resp->headers_.find("Server") == resp->headers_.end()) {
        headers << "Server: Webserv/1.0\r\n";
    }

    // Add Content-Type if set
    if (!resp->content_type_.empty()) {
        headers << "Content-Type: " << resp->content_type_ << "\r\n";
    }

    // Add Content-Length if body exists
    headers << "Content-Length: " << resp->content_length_ << "\r\n";

    // Add custom headers
    for (std::map<std::string, std::string>::const_iterator it =
             resp->headers_.begin();
         it != resp->headers_.end(); ++it) {
        headers << it->first << ": " << it->second << "\r\n";
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

    HttpResponse* resp = conn->response_data_;

    // Add body to write buffer if it's not empty
    if (resp->content_length_ > 0) {
        conn->write_buffer_.insert(conn->write_buffer_.end(),
                                   resp->body_.begin(), resp->body_.end());
    }
    return true;
}

// Generates a standard error response using ErrorHandler
// and writes it to the connection's write buffer.
void ResponseWriter::write_error_response(Connection* conn) {
    if (!conn || !conn->response_data_) {
        return;
    }

    int status_code = conn->response_data_->status_code_;
    if (status_code <= 0) {
        status_code = 500;  // Default to internal server error
    }

    ErrorHandler::apply_to_connection(conn, status_code,
                                      *(conn->virtual_server_));

    // Write the response to the buffer
    write_headers(conn);
    write_body(conn);
}

std::string ResponseWriter::get_current_gmt_time() const {
    char buffer[100];
    time_t now = time(NULL);
    struct tm* tm_info = gmtime(&now);

    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    return std::string(buffer);
}