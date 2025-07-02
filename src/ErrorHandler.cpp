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

    // TODO: Update connection state to writing only write_buffer_ is not empty
    // serialize_response_to_buffer(conn);
    conn->conn_state_ = CONN_WRITING_RESPONSE;

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
    resp->body_.clear();

    // Set status code and message
    resp->status_code_ = response_status;
    resp->status_message_ = get_status_message(response_status);

    // Get error page content (custom or default)
    std::string content = get_error_page_content(response_status, config);

    // Set response body and headers
    resp->body_.assign(content.begin(), content.end());

    // Set headers
    resp->set_header("Content-Type", "text/html; charset=UTF-8");

    // Convert size to string (C++98 compatible)
    std::ostringstream content_length;
    content_length << resp->body_.size();
    resp->set_header("Content-Length", content_length.str());

    log(LOG_DEBUG, "Generated error page for status %d (%zu bytes)",
        response_status, resp->body_.size());
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

// TODO: Leave serialization logic in ResponseWriter (this is temporary)
void serialize_response_to_buffer(Connection* conn) {
    if (!conn || !conn->response_data_) {
        log(LOG_FATAL,
            "serialize_response_to_buffer: Invalid connection or response "
            "data");
        return;
    }

    // Clear existing write buffer
    conn->write_buffer_.reset();

    // Serialize headers
    std::string headers = conn->response_data_->get_headers_string();
    if ((ssize_t)conn->write_buffer_.append(headers.data(), headers.size()) ==
        BUFFER_FULL) {
        log(LOG_ERROR,
            "serialize_response_to_buffer: Write buffer full while appending "
            "headers");
        return;
    }

    // Append body if present
    std::vector<char>& body = conn->response_data_->body_;
    if (!body.empty()) {
        if ((ssize_t)conn->write_buffer_.append(body.data(), body.size()) ==
            BUFFER_FULL) {
            log(LOG_ERROR,
                "serialize_response_to_buffer: Write buffer full while "
                "appending body");
            return;
        }
    }
}

// ==================== ERROR PAGE GENERATION ====================

// TODO: Set response_data->body_fd_ to a file descriptor if the error page
// is a file, so it can be sent directly without loading into memory
std::string ErrorHandler::get_error_page_content(int response_status,
                                                 const VirtualServer& config) {
    // Check if custom error page is configured
    std::map<int, std::string>::const_iterator it =
        config.error_pages_.find(response_status);

    if (it != config.error_pages_.end()) {
        // Custom error page found, try to read the file
        std::string error_page_path = it->second;

        std::ifstream file(error_page_path.c_str());
        if (file.is_open()) {
            std::string content;
            std::string line;
            while (std::getline(file, line)) {
                content += line + "\n";
            }
            file.close();

            if (!content.empty()) {
                log(LOG_DEBUG, "Loaded custom error page: %s",
                    error_page_path.c_str());
                return content;
            } else {
                log(LOG_WARNING, "Custom error page is empty: %s",
                    error_page_path.c_str());
            }
        } else {
            log(LOG_WARNING, "Could not read custom error page: %s",
                error_page_path.c_str());
        }
    }

    // No custom page or couldn't read it, generate default
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
