 #include "webserv.hpp"

ErrorHandler::ErrorHandler(const ServerConfig& config) 
    : config_(config) {
}

ErrorHandler::~ErrorHandler() {
}

void ErrorHandler::handle_error(HttpResponse* resp, int status_code) {
    if (!resp) {
        return;
    }
    
    // Set status code and message
    resp->status_code_ = status_code;
    resp->status_message_ = get_status_message(status_code);
    
    // Get error page content (custom or default)
    std::string content = get_error_page_content(status_code);
    
    // Set response body and headers
    resp->content_type_ = "text/html";
    resp->body_.assign(content.begin(), content.end());
    resp->content_length_ = resp->body_.size();
}

void ErrorHandler::bad_request(HttpResponse* resp) {
    handle_error(resp, 400);
}

void ErrorHandler::unauthorized(HttpResponse* resp) {
    handle_error(resp, 401);
}

void ErrorHandler::forbidden(HttpResponse* resp) {
    handle_error(resp, 403);
}

void ErrorHandler::not_found(HttpResponse* resp) {
    handle_error(resp, 404);
}

void ErrorHandler::method_not_allowed(HttpResponse* resp) {
    handle_error(resp, 405);
}

void ErrorHandler::payload_too_large(HttpResponse* resp) {
    handle_error(resp, 413);
}

void ErrorHandler::unsupported_media_type(HttpResponse* resp) {
    handle_error(resp, 415);
}

void ErrorHandler::internal_server_error(HttpResponse* resp) {
    handle_error(resp, 500);
}

void ErrorHandler::not_implemented(HttpResponse* resp) {
    handle_error(resp, 501);
}

void ErrorHandler::apply_to_connection(Connection* conn, int status_code) {
    if (!conn || !conn->response_data_) {
        return;
    }
    
    // Apply error to response
    handle_error(conn->response_data_, status_code);
    
    // Update connection state to writing
    conn->state_ = Connection::CONN_WRITING;
}

std::string ErrorHandler::get_error_page_content(int status_code) {
    // Check if custom error page is configured
    std::map<int, std::string>::const_iterator it = config_.error_pages.find(status_code);
    
    if (it != config_.error_pages.end()) {
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
    return generate_default_error_page(status_code, get_status_message(status_code));
}

std::string ErrorHandler::generate_default_error_page(int status_code, const std::string& status_message) {
    std::ostringstream html;
    
    html << "<!DOCTYPE html>\n"
         << "<html>\n"
         << "<head>\n"
         << "    <title>" << status_code << " " << status_message << "</title>\n"
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

std::string ErrorHandler::get_status_message(int code) const {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        case 505: return "HTTP Version Not Supported";
        default:  return "Unknown";
    }
}