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

std::string HttpResponse::get_header(const std::string& name) const {
    // Case-insensitive lookup for headers
    std::string lower_name = name;
    for (size_t i = 0; i < lower_name.size(); ++i) {
        lower_name[i] = std::tolower(static_cast<unsigned char>(lower_name[i]));
    }

    std::map<std::string, std::string>::const_iterator it =
        headers_.find(lower_name);
    if (it != headers_.end()) {
        log(LOG_DEBUG, "Response header retrieved: '%s: %s'",
            lower_name.c_str(), it->second.c_str());
        return it->second;
    }
    log(LOG_DEBUG, "Response header '%s' not found", lower_name.c_str());
    return "";
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

     // Add Content-Type if not already set in headers
    if (headers_.find("content-type") == headers_.end() &&
        !content_type_.empty()) {
        oss << "Content-Type: " << content_type_ << "\r\n";
    }

    // Add Content-Length if not already set in headers
    if (headers_.find("content-length") == headers_.end()) {
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