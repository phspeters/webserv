#include "webserv.hpp"

int ErrorHandler::get_parse_message_status(codes::ParseStatus parse_status) {
    int status_code = 500;  // Default to Internal Server Error

    switch (parse_status) {
        case codes::PARSE_ERROR:
        case codes::PARSE_INVALID_REQUEST_LINE:
        case codes::PARSE_INVALID_PATH:
        case codes::PARSE_INVALID_QUERY_STRING:
        case codes::PARSE_MISSING_HOST_HEADER:
        case codes::PARSE_INVALID_CONTENT_LENGTH:
        case codes::PARSE_INVALID_CHUNK_SIZE:
            status_code = codes::BAD_REQUEST;
            break;
        case codes::PARSE_METHOD_NOT_ALLOWED:
            status_code = codes::METHOD_NOT_ALLOWED;
            break;
        case codes::PARSE_CONTENT_TOO_LARGE:
            status_code = codes::PAYLOAD_TOO_LARGE;
            break;
        case codes::PARSE_REQUEST_TOO_LONG:
            status_code = codes::URI_TOO_LONG;
            break;
        case codes::PARSE_HEADER_TOO_LONG:
        case codes::PARSE_TOO_MANY_HEADERS:
            status_code = codes::HEADER_TOO_LONG;
            break;
        case codes::PARSE_VERSION_NOT_SUPPORTED:
            status_code = codes::HTTP_VERSION_NOT_SUPPORTED;
            break;
        case codes::PARSE_MISSING_CONTENT_LENGTH:
            status_code = codes::LENGTH_REQUIRED;
            break;
        case codes::PARSE_UNKNOWN_ENCODING:
            status_code = codes::NOT_IMPLEMENTED;
            break;
        default:
            status_code = codes::INTERNAL_SERVER_ERROR;
            break;
    }

    return status_code;
}

void ErrorHandler::generate_error_response(Connection* conn,
                                           codes::ResponseStatus status_code) {
    if (!conn || !conn->response_data_) {
        log(LOG_ERROR,
            "generate_error_response: Invalid connection or response data");
        return;
    }

    // Check if this is a valid error state
    if (conn->parse_status_ == codes::PARSE_SUCCESS ||
        conn->parse_status_ == codes::PARSE_INCOMPLETE) {
        log(LOG_WARNING,
            "generate_error_response called with non-error parse status: %d",
            static_cast<int>(conn->parse_status_));
        return;
    }

    // Get error info from parse status
    if (status_code == codes::UNDEFINED) {
        int parse_status_code = get_parse_message_status(conn->parse_status_);
        handle_error(conn->response_data_, parse_status_code,
                     *conn->virtual_server_);

    } else {  // Use provided status code
        handle_error(conn->response_data_, status_code, *conn->virtual_server_);
    }

    // Set additional headers
    conn->response_data_->set_header("connection", "close");
    conn->response_data_->set_header("server", "webserv/1.0");
    conn->response_data_->set_header("date", get_current_gmt_time());

    // Update connection state
    conn->conn_state_ = codes::CONN_WRITING;

    log(LOG_INFO, "Generated error response %d for client_fd %d: %s",
        status_code, conn->client_fd_);
}

void ErrorHandler::handle_error(HttpResponse* resp, int status_code,
                                const VirtualServer& config) {
    if (!resp) {
        log(LOG_ERROR, "handle_error: NULL response pointer");
        return;
    }

    // Clear any existing response data
    resp->headers_.clear();
    resp->body_.clear();

    // Set status code and message
    resp->status_code_ = status_code;
    resp->status_message_ = get_status_message(status_code);

    // Get error page content (custom or default)
    std::string content = get_error_page_content(status_code, config);

    // Set response body and headers
    resp->body_.assign(content.begin(), content.end());

    // Set headers
    resp->set_header("Content-Type", "text/html; charset=UTF-8");

    // Convert size to string (C++98 compatible)
    std::ostringstream content_length;
    content_length << resp->body_.size();
    resp->set_header("Content-Length", content_length.str());

    log(LOG_DEBUG, "Generated error page for status %d (%zu bytes)",
        status_code, resp->body_.size());
}

// ==================== ERROR PAGE GENERATION ====================

std::string ErrorHandler::get_error_page_content(int status_code,
                                                 const VirtualServer& config) {
    // Check if custom error page is configured
    std::map<int, std::string>::const_iterator it =
        config.error_pages_.find(status_code);

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
    log(LOG_DEBUG, "Generating default error page for status %d", status_code);
    return generate_default_error_page(status_code,
                                       get_status_message(status_code));
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
