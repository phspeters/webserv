#include "common.hpp"

void ErrorHandler::generate_error_response(Connection* conn,
                                           ResponseStatus response_status) {
    if (!conn || !conn->response_data_) {
        log(LOG_FATAL,
            "generate_error_response: Invalid connection or response data");
        return;
    }

    handle_error(conn->response_data_, response_status, *conn->virtual_server_);

    // Set additional headers
    conn->response_data_->set_header("connection", "close");
    conn->response_data_->set_header("server", "webserv/1.0");
    conn->response_data_->set_header("date", get_current_gmt_time());

    // Update connection state to writing
    conn->conn_state_ = CONN_WRITING_RESPONSE;

    std::string status_msg = get_status_message(response_status);
    log(LOG_INFO, "Generated error response %d for client_fd %d: %s",
        response_status, conn->client_fd_, status_msg.c_str());
}

void ErrorHandler::generate_error_response(Connection* conn,
                                           ParseStatus parse_status) {
    if (!conn || !conn->response_data_) {
        log(LOG_FATAL,
            "generate_error_response: Invalid connection or response data");
        return;
    }

    int response_status = get_parse_message_status(parse_status);
    handle_error(conn->response_data_, response_status, *conn->virtual_server_);

    // Set additional headers
    conn->response_data_->set_header("connection", "close");
    conn->response_data_->set_header("server", "webserv/1.0");
    conn->response_data_->set_header("date", get_current_gmt_time());

    // Set error-specific headers if any
    if (!conn->response_data_->error_headers_.empty()) {
        for (std::map<std::string, std::string>::const_iterator it =
                 conn->response_data_->error_headers_.begin();
             it != conn->response_data_->error_headers_.end(); ++it) {
            conn->response_data_->set_header(it->first, it->second);
        }
    }

    // Use ResponseWriter to serialize the response to buffer
    ResponseWriter response_writer;
    response_writer.write_response_to_buffer(conn);

    // Only set connection state to writing if there's data in the write buffer
    if (!conn->write_buffer_.empty()) {
        conn->conn_state_ = CONN_WRITING_RESPONSE;
    }

    std::string status_msg = get_status_message(parse_status);
    log(LOG_INFO, "Generated error response %d for client_fd %d: %s",
        parse_status, conn->client_fd_, status_msg.c_str());
}

void ErrorHandler::handle_error(HttpResponse* resp, int response_status,
                                const VirtualServer& config) {
    if (!resp) {
        log(LOG_FATAL, "handle_error: NULL response pointer");
        return;
    }

    resp->headers_.clear();
    resp->body_data_.clear();
    resp->body_fd_ = -1;  // Reset body_fd

    // Set status code and message
    resp->status_code_ = response_status;
    resp->status_message_ = get_status_message(response_status);

    // Get error page content (custom or default)
    int body_fd = -1;
    std::string content =
        get_error_page_content(response_status, config, body_fd);

    if (content.empty() && body_fd != -1) {
        // Custom error page file opened successfully, use body_fd
        resp->body_fd_ = body_fd;
        log(LOG_DEBUG, "Using custom error page file (fd: %d) for status %d",
            body_fd, response_status);
    } else {
        // Use generated content (either default or fallback)
        resp->body_data_.assign(content.begin(), content.end());
        resp->body_fd_ = -1;  // Ensure body_fd is invalid when using body_
    }

    // Set headers
    resp->set_header("Content-Type", "text/html; charset=UTF-8");

    if (resp->body_fd_ != -1) {
        // For file-based responses, get file size for Content-Length
        struct stat file_stat;
        if (fstat(resp->body_fd_, &file_stat) == 0) {
            std::ostringstream content_length;
            content_length << file_stat.st_size;
            resp->set_header("Content-Length", content_length.str());
            log(LOG_DEBUG,
                "Using custom error page file (fd: %d, size: %ld bytes) for "
                "status %d",
                resp->body_fd_, file_stat.st_size, response_status);
        } else {
            log(LOG_WARNING, "Could not get file size for error page fd %d: %s",
                resp->body_fd_, strerror(errno));
            // Remove Content-Length header for file-based responses if we can't
            // get size
            resp->headers_.erase("Content-Length");
        }
    } else {
        // For generated content, set Content-Length from body size
        std::ostringstream content_length;
        content_length << resp->body_data_.size();
        resp->set_header("Content-Length", content_length.str());
        log(LOG_DEBUG, "Generated error page for status %d (%zu bytes)",
            response_status, resp->body_data_.size());
    }
}

int ErrorHandler::get_parse_message_status(ParseStatus parse_status) {
    int status_code = 500;  // Default to Internal Server Error

    switch (parse_status) {
        case PARSE_ERROR:
        case PARSE_INVALID_REQUEST_LINE:
        case PARSE_INVALID_PATH:
        case PARSE_INVALID_QUERY_STRING:
        case PARSE_MISSING_HOST_HEADER:
        case PARSE_INVALID_CONTENT_LENGTH:
        case PARSE_INVALID_CHUNK_SIZE:
            status_code = BAD_REQUEST;
            break;
        case PARSE_METHOD_NOT_ALLOWED:
            status_code = METHOD_NOT_ALLOWED;
            break;
        case PARSE_CONTENT_TOO_LARGE:
            status_code = PAYLOAD_TOO_LARGE;
            break;
        case PARSE_REQUEST_TOO_LONG:
            status_code = URI_TOO_LONG;
            break;
        case PARSE_HEADER_TOO_LONG:
        case PARSE_TOO_MANY_HEADERS:
            status_code = HEADER_TOO_LONG;
            break;
        case PARSE_VERSION_NOT_SUPPORTED:
            status_code = HTTP_VERSION_NOT_SUPPORTED;
            break;
        case PARSE_MISSING_CONTENT_LENGTH:
            status_code = LENGTH_REQUIRED;
            break;
        case PARSE_UNKNOWN_ENCODING:
        case PARSE_METHOD_NOT_IMPLEMENTED:
            status_code = NOT_IMPLEMENTED;
            break;
        default:
            status_code = INTERNAL_SERVER_ERROR;
            break;
    }

    return status_code;
}

// Serialization logic has been moved to ResponseWriter class
// This function is no longer needed as
// ResponseWriter::write_response_to_buffer() handles all serialization properly

// ==================== ERROR PAGE GENERATION ====================

// Set response_data->body_fd_ to a file descriptor if the error page
// is a file, so it can be sent directly without loading into memory
std::string ErrorHandler::get_error_page_content(int response_status,
                                                 const VirtualServer& config,
                                                 int& body_fd) {
    body_fd = -1;  // Initialize to invalid fd

    // Check if custom error page is configured
    std::map<int, std::string>::const_iterator it =
        config.error_pages_.find(response_status);

    if (it != config.error_pages_.end()) {
        // Custom error page found, try to open the file
        std::string error_page_path = it->second;

        // Try to open the file for reading
        body_fd = open(error_page_path.c_str(), O_RDONLY);
        if (body_fd != -1) {
            // File opened successfully, return empty string and set body_fd
            log(LOG_DEBUG, "Opened custom error page file: %s (fd: %d)",
                error_page_path.c_str(), body_fd);
            return "";  // Empty string indicates file should be used via
                        // body_fd
        } else {
            log(LOG_WARNING, "Could not open custom error page: %s (%s)",
                error_page_path.c_str(), strerror(errno));
            body_fd = -1;  // Ensure it's invalid
        }
    }

    // No custom page or couldn't open it, generate default
    log(LOG_DEBUG, "Generating default error page for status %d",
        response_status);
    return generate_default_error_page(response_status,
                                       get_status_message(response_status));
}

std::string ErrorHandler::generate_default_error_page(
    int status_code, const std::string& status_message) {
    std::ostringstream html;

    std::ostringstream status_code_str;
    status_code_str << status_code;

    html << "<!DOCTYPE html>\n"
         << "<html>\n"
         << "<head>\n"
         << "    <title>" << status_code_str.str() << " " << status_message
         << "</title>\n"
         << "    <meta charset=\"UTF-8\">\n"
         << "    <style>\n"
         << "        body {\n"
         << "            font-family: Arial, sans-serif;\n"
         << "            text-align: center;\n"
         << "            margin: 0;\n"
         << "            padding: 50px 20px;\n"
         << "            background-color: #f8f9fa;\n"
         << "            color: #333;\n"
         << "        }\n"
         << "        .container {\n"
         << "            max-width: 600px;\n"
         << "            margin: 0 auto;\n"
         << "            background: white;\n"
         << "            padding: 40px;\n"
         << "            border-radius: 8px;\n"
         << "            box-shadow: 0 2px 10px rgba(0,0,0,0.1);\n"
         << "        }\n"
         << "        .error-code {\n"
         << "            font-size: 72px;\n"
         << "            font-weight: bold;\n"
         << "            margin-bottom: 20px;\n"
         << "            color: #e74c3c;\n"
         << "        }\n"
         << "        .error-message {\n"
         << "            font-size: 24px;\n"
         << "            margin-bottom: 20px;\n"
         << "            color: #2c3e50;\n"
         << "        }\n"
         << "        .error-description {\n"
         << "            font-size: 16px;\n"
         << "            color: #7f8c8d;\n"
         << "            margin-bottom: 30px;\n"
         << "        }\n"
         << "        .footer {\n"
         << "            font-size: 12px;\n"
         << "            color: #95a5a6;\n"
         << "            border-top: 1px solid #ecf0f1;\n"
         << "            padding-top: 20px;\n"
         << "            margin-top: 30px;\n"
         << "        }\n"
         << "    </style>\n"
         << "</head>\n"
         << "<body>\n"
         << "    <div class=\"container\">\n"
         << "        <div class=\"error-code\">" << status_code_str.str()
         << "</div>\n"
         << "        <div class=\"error-message\">" << status_message
         << "</div>\n"
         << "        <div class=\"error-description\">\n"
         << "            The server encountered an error and could not "
            "complete your request.\n"
         << "        </div>\n"
         << "        <div class=\"footer\">\n"
         << "            webserv/1.0\n"
         << "        </div>\n"
         << "    </div>\n"
         << "</body>\n"
         << "</html>";

    return html.str();
}
