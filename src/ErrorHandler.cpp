#include "webserv.hpp"

void ErrorHandler::handle_error(HttpResponse* resp, int status_code,
                                const VirtualServer& config) {
    if (!resp) {
        return;
    }

    // Set status code and message
    resp->status_code_ = status_code;
    resp->status_message_ = HttpResponse::get_status_message(status_code);

    // Get error page content (custom or default)
    std::string content = get_error_page_content(status_code, config);

    // Set response body and headers
    resp->content_type_ = "text/html";
    resp->body_.assign(content.begin(), content.end());
    resp->content_length_ = resp->body_.size();
}

void ErrorHandler::bad_request(HttpResponse* resp,
                               const VirtualServer& config) {
    handle_error(resp, 400, config);
}

void ErrorHandler::unauthorized(HttpResponse* resp,
                                const VirtualServer& config) {
    handle_error(resp, 401, config);
}

void ErrorHandler::forbidden(HttpResponse* resp, const VirtualServer& config) {
    handle_error(resp, 403, config);
}

void ErrorHandler::not_found(HttpResponse* resp, const VirtualServer& config) {
    handle_error(resp, 404, config);
}

void ErrorHandler::method_not_allowed(HttpResponse* resp,
                                      const VirtualServer& config) {
    handle_error(resp, 405, config);
}

void ErrorHandler::payload_too_large(HttpResponse* resp,
                                     const VirtualServer& config) {
    handle_error(resp, 413, config);
}

void ErrorHandler::unsupported_media_type(HttpResponse* resp,
                                          const VirtualServer& config) {
    handle_error(resp, 415, config);
}

void ErrorHandler::internal_server_error(HttpResponse* resp,
                                         const VirtualServer& config) {
    handle_error(resp, 500, config);
}

void ErrorHandler::not_implemented(HttpResponse* resp,
                                   const VirtualServer& config) {
    handle_error(resp, 501, config);
}

void ErrorHandler::request_timeout(HttpResponse* resp,
                                   const VirtualServer& config) {
    handle_error(resp, 408, config);
}

void ErrorHandler::too_many_requests(HttpResponse* resp,
                                     const VirtualServer& config,
                                     int retry_after) {
    handle_error(resp, 429, config);

    // Add Retry-After header
    std::ostringstream ss;
    ss << retry_after;
    resp->headers_["Retry-After"] = ss.str();
}

void ErrorHandler::apply_to_connection(Connection* conn, int status_code,
                                       const VirtualServer& virtual_server) {
    if (!conn || !conn->response_data_) {
        return;
    }

    // Apply error to response
    handle_error(conn->response_data_, status_code, virtual_server);

    // Update connection state to writing
    conn->conn_state_ = codes::CONN_WRITING;
}

std::string ErrorHandler::get_error_page_content(int status_code,
                                                 const VirtualServer& config) {
    // Check if custom error page is configured
    std::map<int, std::string>::const_iterator it =
        config.error_pages_.find(status_code);

    if (it != config.error_pages_.end()) {
        // Custom error page found, try to read the file
        std::string error_page_path = it->second;

        std::ifstream file(error_page_path.c_str(), std::ios::binary);
        if (file.is_open()) {
            // Read the file content
            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
            file.close();

            if (!content.empty()) {
                return content;
            }
        }
    }

    // No custom page or couldn't read it, generate default
    return generate_default_error_page(
        status_code, HttpResponse::get_status_message(status_code));
}

std::string ErrorHandler::generate_default_error_page(
    int status_code, const std::string& status_message) {
    std::ostringstream html;

    html << "<!DOCTYPE html>\n"
         << "<html>\n"
         << "<head>\n"
         << "    <title>" << status_code << " " << status_message
         << "</title>\n"
         << "    <style>\n"
         << "        body {\n"
         << "            font-family: Arial, sans-serif;\n"
         << "            text-align: center;\n"
         << "            margin-top: 50px;\n"
         << "            background-color: #f5f5f5;\n"
         << "        }\n"
         << "        h1 {\n"
         << "            color: #444;\n"
         << "        }\n"
         << "        .error-code {\n"
         << "            font-size: 72px;\n"
         << "            margin-bottom: 0;\n"
         << "            color: #e74c3c;\n"
         << "        }\n"
         << "        .error-message {\n"
         << "            font-size: 24px;\n"
         << "            margin-top: 0;\n"
         << "            color: #7f8c8d;\n"
         << "        }\n"
         << "    </style>\n"
         << "</head>\n"
         << "<body>\n"
         << "    <h1 class=\"error-code\">" << status_code << "</h1>\n"
         << "    <p class=\"error-message\">" << status_message << "</p>\n"
         << "    <p>The server could not process your request.</p>\n"
         << "</body>\n"
         << "</html>";

    return html.str();
}
