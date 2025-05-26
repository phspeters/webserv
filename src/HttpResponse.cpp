#include "webserv.hpp"

HttpResponse::HttpResponse() : status_code_(codes::OK), content_length_(0) {
    version_ = "HTTP/1.1";
}

HttpResponse::~HttpResponse() {}

void HttpResponse::set_header(const std::string& name,
                              const std::string& value) {
    // Case-insensitive lookup for headers
    std::string lower_name = name;
    for (size_t i = 0; i < lower_name.size(); ++i) {
        lower_name[i] = std::tolower(static_cast<unsigned char>(lower_name[i]));
    }

    headers_[lower_name] = value;

    log(LOG_DEBUG, "Response header set: '%s: %s'", lower_name.c_str(),
        value.c_str());
}

void HttpResponse::set_status(int code) {
    status_code_ = code;
    status_message_ = get_status_message(code);

    log(LOG_DEBUG, "Response status set: %d %s", status_code_,
        status_message_.c_str());
}

std::string HttpResponse::get_status_message(int code) {
    switch (code) {
        // 2xx - Success
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 206:
            return "Partial Content";

        // 3xx - Redirection
        case 301:
            return "Moved Permanently";
        case 302:
            return "Found";
        case 303:
            return "See Other";
        case 304:
            return "Not Modified";
        case 307:
            return "Temporary Redirect";
        case 308:
            return "Permanent Redirect";

        // 4xx - Client Error
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
        case 406:
            return "Not Acceptable";
        case 408:
            return "Request Timeout";
        case 409:
            return "Conflict";
        case 413:
            return "Payload Too Large";
        case 414:
            return "URI Too Long";
        case 415:
            return "Unsupported Media Type";
        case 429:
            return "Too Many Requests";
        case 431:
            return "Request Header Fields Too Large";

        // 5xx - Server Error
        case 500:
            return "Internal Server Error";
        case 501:
            return "Not Implemented";
        case 502:
            return "Bad Gateway";
        case 503:
            return "Service Unavailable";
        case 504:
            return "Gateway Timeout";
        case 505:
            return "HTTP Version Not Supported";

        default:
            return "Unknown Status";
    }
}

std::string HttpResponse::get_status_line() const {
    std::ostringstream oss;
    oss << version_ << " " << status_code_ << " " << status_message_;

    log(LOG_TRACE, "Response status line: %s", oss.str().c_str());

    return oss.str();
}

std::string HttpResponse::get_headers_string() const {
    std::ostringstream oss;

    // Add status line
    oss << get_status_line() << "\r\n";

    // Add headers
    for (std::map<std::string, std::string>::const_iterator it =
             headers_.begin();
         it != headers_.end(); ++it) {
        oss << it->first << ": " << it->second << "\r\n";
    }

    // Add Content-Type and Content-Length if not already set in headers
    if (headers_.find("Content-Type") == headers_.end() &&
        !content_type_.empty()) {
        oss << "Content-Type: " << content_type_ << "\r\n";
    }

    if (headers_.find("Content-Length") == headers_.end()) {
        oss << "Content-Length: " << content_length_ << "\r\n";
    }

    // End of headers
    oss << "\r\n";

    log(LOG_TRACE, "Response headers string: %s", oss.str().c_str());

    return oss.str();
}

void HttpResponse::clear() {
    status_code_ = 0;
    status_message_.clear();
    version_ = "HTTP/1.1";
    headers_.clear();
    body_.clear();
    content_length_ = 0;
    content_type_.clear();

    log(LOG_TRACE, "HttpResponse cleared");
}