#include "webserv.hpp"

ResponseWriter::ResponseWriter(const ServerConfig& config) : config_(config) {}

ResponseWriter::~ResponseWriter() {}

ResponseWriter::ResponseStatus ResponseWriter::write_response(Connection* conn) {
    // Validate connection
    if (!conn || conn->client_fd_ < 0) {
        return RESPONSE_ERROR;
    }

    // If buffer is empty, prepare the response data first
    if (conn->write_buffer_.empty()) {
        // Write headers
        if (!write_headers(conn)) {
            return RESPONSE_ERROR;
        }

        // Write body
        if (!write_body(conn)) {
            return RESPONSE_ERROR;
        }
    }

    // Nothing to send
    if (conn->write_buffer_.empty()) {
        return RESPONSE_COMPLETE;
    }

    // Send the response
    ssize_t bytes_written =
        send(conn->client_fd_, conn->write_buffer_.data(),
             conn->write_buffer_.size(),
             MSG_NOSIGNAL);  // MSG_NOSIGNAL prevents SIGPIPE signal if the
                             // client has closed the connection

    // Check for errors or closed connection
    if (bytes_written < 0) {
        // If EAGAIN/EWOULDBLOCK, it means the socket buffer is full, try again later
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return RESPONSE_INCOMPLETE;
        }
        // For other errors, the connection is probably broken
        return RESPONSE_ERROR;
    } else if (bytes_written == 0) {
        // Connection closed by client
        return RESPONSE_ERROR;
    }

    // Remove the written data from the buffer
    conn->write_buffer_.erase(conn->write_buffer_.begin(),
                              conn->write_buffer_.begin() + bytes_written);

    // Update the last activity timestamp
    conn->last_activity_ = time(NULL);

    // If buffer is empty, we're done
    if (conn->write_buffer_.empty()) {
        return RESPONSE_COMPLETE;
    }
    
    // More data to send
    return RESPONSE_INCOMPLETE;
}

bool ResponseWriter::write_headers(Connection* conn) {
    if (!conn) {
        return false;
    }

    HttpResponse* resp = conn->response_data_;

    // Format the response status line and headers
    std::stringstream headers;

    // Add status line
    headers << resp->version_ << " " << resp->status_code_ << " "
            << resp->status_message_ << "\r\n";

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
        conn->write_buffer_.insert(conn->write_buffer_.end(), resp->body_.begin(),
                                  resp->body_.end());
    }
    return true;
}

void ResponseWriter::write_error_response(Connection* conn) {
    if (!conn) {
        return;
    }

    // Create a basic error response
    HttpResponse* resp = conn->response_data_;
    if (!resp) {
        return;
    }

    resp->status_message_ = get_status_message(resp->status_code_);
    resp->content_type_ = "text/html";

    std::ostringstream ss;
    ss << resp->status_code_;

    std::string body = "<html><body><h1>" + ss.str() + " " +
                       resp->status_message_ + "</h1></body></html>";

    resp->body_.assign(body.begin(), body.end());
    resp->content_length_ = resp->body_.size();

    write_headers(conn);

    // Add body to write buffer
    conn->write_buffer_.insert(conn->write_buffer_.end(), resp->body_.begin(),
                               resp->body_.end());
}

std::string ResponseWriter::get_status_message(int code) const {
    switch (code) {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 301:
            return "Moved Permanently";
        case 302:
            return "Found";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 413:
            return "Payload Too Large";
        case 414:
            return "URI Too Long";
        case 415:
            return "Unsupported Media Type";
        case 500:
            return "Internal Server Error";
        case 501:
            return "Not Implemented";
        case 503:
            return "Service Unavailable";
        case 505:
            return "HTTP Version Not Supported";
        default:
            return "Unknown";
    }
}

std::string ResponseWriter::get_current_gmt_time() const {
    char buffer[100];
    time_t now = time(NULL);
    struct tm* tm_info = gmtime(&now);

    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    return std::string(buffer);
}