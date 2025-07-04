#include "common.hpp"

void ErrorHandler::generate_error_response(Connection* conn,
                                           ParseStatus parse_status) {
    ResponseStatus response_status =
        parse_status_to_response_status(parse_status);
    generate_error_response(conn, response_status);
}

void ErrorHandler::generate_error_response(Connection* conn,
                                           ResponseStatus response_status) {
    if (!conn || !conn->response_data_) {
        log(LOG_FATAL,
            "generate_error_response: Invalid connection or response data");
        return;
    }

    HttpResponse* resp = conn->response_data_;

    resp->status_code_ = response_status;
    resp->status_message_ = get_status_message(response_status);

    resp->headers_.clear();
    resp->body_data_.clear();
    resp->body_fd_ = -1;

    int error_page_fd = get_error_page(response_status, *conn->virtual_server_);
    if (error_page_fd < 0) {
        // Generate default error page content
        std::string default_error_page =
            generate_default_error_page(response_status, resp->status_message_);
        resp->body_data_.assign(default_error_page.begin(),
                                default_error_page.end());
        log(LOG_DEBUG, "Generated default error page for status %d (%zu bytes)",
            response_status, resp->body_data_.size());

        std::ostringstream content_length_stream;
        content_length_stream << resp->body_data_.size();
        resp->set_header("content-length", content_length_stream.str());
    } else {
        // Custom error page file opened successfully
        resp->body_fd_ = error_page_fd;
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
            resp->headers_.erase("Content-Length");
        }
    }

    // Set additional headers
    resp->set_header("content-type", "text/html; charset=UTF-8");
    resp->set_header("connection", "close");
    resp->set_header("server", "webserv/1.0");
    resp->set_header("date", get_current_gmt_time());

    // Set error-specific headers if any
    if (!resp->error_headers_.empty()) {
        for (std::map<std::string, std::string>::const_iterator it =
                 resp->error_headers_.begin();
             it != resp->error_headers_.end(); ++it) {
            resp->set_header(it->first, it->second);
        }
    }

    log(LOG_INFO, "Generated error response %d for client_fd %d: %s",
        response_status, conn->client_fd_, resp->status_message_.c_str());
}

int ErrorHandler::get_error_page(int response_status,
                                 const VirtualServer& config) {
    int error_fd = -1;

    std::map<int, std::string>::const_iterator it =
        config.error_pages_.find(response_status);

    if (it != config.error_pages_.end()) {
        std::string error_page_path = it->second;

        error_fd = open(error_page_path.c_str(), O_RDONLY);
        if (error_fd != -1) {
            log(LOG_DEBUG, "Opened custom error page file: %s (fd: %d)",
                error_page_path.c_str(), error_fd);
            return error_fd;
        } else {
            log(LOG_WARNING, "Could not open custom error page: %s (%s)",
                error_page_path.c_str(), strerror(errno));
        }
    }

    return error_fd;
}

std::string ErrorHandler::generate_default_error_page(
    ResponseStatus status_code, const std::string& status_message) {
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

ResponseStatus ErrorHandler::parse_status_to_response_status(
    ParseStatus parse_status) {
    ResponseStatus status_code = INTERNAL_SERVER_ERROR;

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
