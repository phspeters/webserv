#include "webserv.hpp"

// ==================== ERROR INFO MAPPING ====================

void ErrorHandler::get_error_info_from_parse_status(codes::ParseStatus parse_status, 
                                                    int& status_code, 
                                                    std::string& message) {
    switch (parse_status) {
        case codes::PARSE_ERROR_BAD_REQUEST:
            status_code = 400;
            message = "Bad Request: Invalid HTTP request format";
            break;
        case codes::PARSE_ERROR_METHOD_NOT_ALLOWED:
            status_code = 405;
            message = "Method Not Allowed";
            break;
        case codes::PARSE_ERROR_ENTITY_TOO_LARGE:
            status_code = 413;
            message = "Payload Too Large";
            break;
        case codes::PARSE_ERROR_URI_TOO_LONG:
            status_code = 414;
            message = "URI Too Long";
            break;
        case codes::PARSE_ERROR_HEADER_TOO_LARGE:
            status_code = 431;
            message = "Request Header Fields Too Large";
            break;
        case codes::PARSE_ERROR_HTTP_VERSION_NOT_SUPPORTED:
            status_code = 505;
            message = "HTTP Version Not Supported";
            break;
        case codes::PARSE_ERROR_REQUEST_TIMEOUT:
            status_code = 408;
            message = "Request Timeout";
            break;
        default:
            status_code = 500;
            message = "Internal Server Error";
            break;
    }
}

void ErrorHandler::get_error_info_from_response_status(codes::ResponseStatus response_status,
                                                      int& status_code,
                                                      std::string& message) {
    switch (response_status) {
        case codes::RESPONSE_ERROR_BAD_REQUEST:
            status_code = 400;
            message = "Bad Request";
            break;
        case codes::RESPONSE_ERROR_UNAUTHORIZED:
            status_code = 401;
            message = "Unauthorized";
            break;
        case codes::RESPONSE_ERROR_FORBIDDEN:
            status_code = 403;
            message = "Forbidden";
            break;
        case codes::RESPONSE_ERROR_NOT_FOUND:
            status_code = 404;
            message = "Not Found";
            break;
        case codes::RESPONSE_ERROR_METHOD_NOT_ALLOWED:
            status_code = 405;
            message = "Method Not Allowed";
            break;
        case codes::RESPONSE_ERROR_REQUEST_TIMEOUT:
            status_code = 408;
            message = "Request Timeout";
            break;
        case codes::RESPONSE_ERROR_PAYLOAD_TOO_LARGE:
            status_code = 413;
            message = "Payload Too Large";
            break;
        case codes::RESPONSE_ERROR_UNSUPPORTED_MEDIA_TYPE:
            status_code = 415;
            message = "Unsupported Media Type";
            break;
        case codes::RESPONSE_ERROR_TOO_MANY_REQUESTS:
            status_code = 429;
            message = "Too Many Requests";
            break;
        case codes::RESPONSE_ERROR_INTERNAL_SERVER_ERROR:
            status_code = 500;
            message = "Internal Server Error";
            break;
        case codes::RESPONSE_ERROR_NOT_IMPLEMENTED:
            status_code = 501;
            message = "Not Implemented";
            break;
        case codes::RESPONSE_ERROR_BAD_GATEWAY:
            status_code = 502;
            message = "Bad Gateway";
            break;
        case codes::RESPONSE_ERROR_SERVICE_UNAVAILABLE:
            status_code = 503;
            message = "Service Unavailable";
            break;
        case codes::RESPONSE_ERROR_GATEWAY_TIMEOUT:
            status_code = 504;
            message = "Gateway Timeout";
            break;
        default:
            status_code = 500;
            message = "Internal Server Error";
            break;
    }
}

// ==================== MAIN ERROR RESPONSE GENERATORS ====================

void ErrorHandler::generate_error_response(Connection* conn) {
    if (!conn || !conn->response_data_) {
        log(LOG_ERROR, "generate_error_response: Invalid connection or response data");
        return;
    }

    // Check if this is a valid error state
    if (conn->parse_status_ == codes::PARSE_COMPLETE || 
        conn->parse_status_ == codes::PARSE_INCOMPLETE) {
        log(LOG_WARNING, "generate_error_response called with non-error parse status: %d", 
            static_cast<int>(conn->parse_status_));
        return;
    }

    // Get error info from parse status
    int status_code;
    std::string error_message;
    get_error_info_from_parse_status(conn->parse_status_, status_code, error_message);
    
    // Generate the error response
    handle_error(conn->response_data_, status_code, *conn->virtual_server_);
    
    // Set additional headers
    conn->response_data_->headers_["Connection"] = "close";
    conn->response_data_->headers_["Server"] = "webserv/1.0";
    conn->response_data_->headers_["Date"] = get_current_gmt_time();
    
    // Update connection state
    conn->response_status_ = codes::RESPONSE_READY;
    conn->conn_state_ = codes::CONN_WRITING;
    
    log(LOG_INFO, "Generated error response %d for client_fd %d: %s", 
        status_code, conn->client_fd_, error_message.c_str());
}

void ErrorHandler::generate_error_response(Connection* conn, codes::ResponseStatus status) {
    if (!conn || !conn->response_data_) {
        log(LOG_ERROR, "generate_error_response: Invalid connection or response data");
        return;
    }
    
    // Get error info from response status
    int status_code;
    std::string error_message;
    get_error_info_from_response_status(status, status_code, error_message);
    
    // Generate the error response
    handle_error(conn->response_data_, status_code, *conn->virtual_server_);
    
    // Set additional headers
    conn->response_data_->headers_["Connection"] = "close";
    conn->response_data_->headers_["Server"] = "webserv/1.0";
    conn->response_data_->headers_["Date"] = get_current_gmt_time();
    
    // Update connection state
    conn->response_status_ = codes::RESPONSE_READY;
    conn->conn_state_ = codes::CONN_WRITING;
    
    log(LOG_INFO, "Generated error response %d for client_fd %d: %s", 
        status_code, conn->client_fd_, error_message.c_str());
}

// ==================== CORE ERROR HANDLING ====================

void ErrorHandler::handle_error(HttpResponse* resp, int status_code, const VirtualServer& config) {
    if (!resp) {
        log(LOG_ERROR, "handle_error: NULL response pointer");
        return;
    }

    // Clear any existing response data
    resp->headers_.clear();
    resp->body_.clear();

    // Set status code and message
    resp->status_code_ = status_code;
    resp->status_message_ = HttpResponse::get_status_message(status_code);

    // Get error page content (custom or default)
    std::string content = get_error_page_content(status_code, config);

    // Set response body and headers
    resp->body_.assign(content.begin(), content.end());
    
    // Set headers
    resp->headers_["Content-Type"] = "text/html; charset=UTF-8";
    
    // Convert size to string (C++98 compatible)
    std::ostringstream content_length;
    content_length << resp->body_.size();
    resp->headers_["Content-Length"] = content_length.str();
    
    log(LOG_DEBUG, "Generated error page for status %d (%zu bytes)", 
        status_code, resp->body_.size());
}

// ==================== SPECIFIC ERROR METHODS ====================

void ErrorHandler::bad_request(HttpResponse* resp, const VirtualServer& config) {
    handle_error(resp, 400, config);
}

void ErrorHandler::unauthorized(HttpResponse* resp, const VirtualServer& config) {
    handle_error(resp, 401, config);
}

void ErrorHandler::forbidden(HttpResponse* resp, const VirtualServer& config) {
    handle_error(resp, 403, config);
}

void ErrorHandler::not_found(HttpResponse* resp, const VirtualServer& config) {
    handle_error(resp, 404, config);
}

void ErrorHandler::method_not_allowed(HttpResponse* resp, const VirtualServer& config) {
    handle_error(resp, 405, config);
}

void ErrorHandler::request_timeout(HttpResponse* resp, const VirtualServer& config) {
    handle_error(resp, 408, config);
}

void ErrorHandler::payload_too_large(HttpResponse* resp, const VirtualServer& config) {
    handle_error(resp, 413, config);
}

void ErrorHandler::unsupported_media_type(HttpResponse* resp, const VirtualServer& config) {
    handle_error(resp, 415, config);
}

void ErrorHandler::too_many_requests(HttpResponse* resp, const VirtualServer& config, int retry_after) {
    handle_error(resp, 429, config);

    // Add Retry-After header
    std::ostringstream ss;
    ss << retry_after;
    resp->headers_["Retry-After"] = ss.str();
}

void ErrorHandler::internal_server_error(HttpResponse* resp, const VirtualServer& config) {
    handle_error(resp, 500, config);
}

void ErrorHandler::not_implemented(HttpResponse* resp, const VirtualServer& config) {
    handle_error(resp, 501, config);
}

void ErrorHandler::bad_gateway(HttpResponse* resp, const VirtualServer& config) {
    handle_error(resp, 502, config);
}

void ErrorHandler::service_unavailable(HttpResponse* resp, const VirtualServer& config) {
    handle_error(resp, 503, config);
}

void ErrorHandler::gateway_timeout(HttpResponse* resp, const VirtualServer& config) {
    handle_error(resp, 504, config);
}

void ErrorHandler::http_version_not_supported(HttpResponse* resp, const VirtualServer& config) {
    handle_error(resp, 505, config);
}

void ErrorHandler::insufficient_storage(HttpResponse* resp, const VirtualServer& config) {
    handle_error(resp, 507, config);
}

// ==================== CONNECTION UTILITIES ====================

void ErrorHandler::apply_to_connection(Connection* conn, int status_code, const VirtualServer& virtual_server) {
    if (!conn || !conn->response_data_) {
        log(LOG_ERROR, "apply_to_connection: Invalid connection data");
        return;
    }

    // Apply error to response
    handle_error(conn->response_data_, status_code, virtual_server);

    // Set connection headers
    conn->response_data_->headers_["Connection"] = "close";
    conn->response_data_->headers_["Server"] = "webserv/1.0";
    conn->response_data_->headers_["Date"] = get_current_gmt_time();

    // Update connection state to writing
    conn->response_status_ = codes::RESPONSE_READY;
    conn->conn_state_ = codes::CONN_WRITING;
    
    log(LOG_DEBUG, "Applied error %d to connection [fd:%d]", status_code, conn->client_fd_);
}

// ==================== ERROR PAGE GENERATION ====================

std::string ErrorHandler::get_error_page_content(int status_code, const VirtualServer& config) {
    // Check if custom error page is configured
    std::map<int, std::string>::const_iterator it = config.error_pages_.find(status_code);

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
                log(LOG_DEBUG, "Loaded custom error page: %s", error_page_path.c_str());
                return content;
            } else {
                log(LOG_WARNING, "Custom error page is empty: %s", error_page_path.c_str());
            }
        } else {
            log(LOG_WARNING, "Could not read custom error page: %s", error_page_path.c_str());
        }
    }

    // No custom page or couldn't read it, generate default
    log(LOG_DEBUG, "Generating default error page for status %d", status_code);
    return generate_default_error_page(status_code, HttpResponse::get_status_message(status_code));
}

std::string ErrorHandler::generate_default_error_page(int status_code, const std::string& status_message) {
    std::ostringstream html;
    
    std::ostringstream status_code_str;
    status_code_str << status_code;

    html << "<!DOCTYPE html>\n"
         << "<html>\n"
         << "<head>\n"
         << "    <title>" << status_code_str.str() << " " << status_message << "</title>\n"
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
         << "        <div class=\"error-code\">" << status_code_str.str() << "</div>\n"
         << "        <div class=\"error-message\">" << status_message << "</div>\n"
         << "        <div class=\"error-description\">\n"
         << "            The server encountered an error and could not complete your request.\n"
         << "        </div>\n"
         << "        <div class=\"footer\">\n"
         << "            webserv/1.0\n"
         << "        </div>\n"
         << "    </div>\n"
         << "</body>\n"
         << "</html>";

    return html.str();
}