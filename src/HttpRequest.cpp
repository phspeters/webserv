#include "webserv.hpp"

HttpRequest::HttpRequest() {}

HttpRequest::~HttpRequest() {}

std::string HttpRequest::get_header(const std::string& name) const {
    // Case-insensitive lookup for headers
    std::string lower_name = name;
    for (size_t i = 0; i < lower_name.size(); ++i) {
        lower_name[i] = std::tolower(static_cast<unsigned char>(lower_name[i]));
    }

    std::map<std::string, std::string>::const_iterator it =
        headers_.find(lower_name);
    if (it != headers_.end()) {
        log(LOG_DEBUG, "Request header retrieved: '%s: %s'", lower_name.c_str(),
            it->second.c_str());
        return it->second;
    }
    log(LOG_DEBUG, "Request header '%s' not found", lower_name.c_str());
    return "";
}

void HttpRequest::set_header(const std::string& name,
                             const std::string& value) {
    // Case-insensitive lookup for headers
    std::string lower_name = name;
    for (size_t i = 0; i < lower_name.size(); ++i) {
        lower_name[i] = std::tolower(static_cast<unsigned char>(lower_name[i]));
    }

    headers_[lower_name] = value;
    log(LOG_DEBUG, "Request header set: '%s: %s'", lower_name.c_str(),
        value.c_str());
}

void HttpRequest::clear() {
    method_.clear();
    uri_.clear();
    version_.clear();
    headers_.clear();
    body_.clear();
    path_.clear();
    query_string_.clear();

    log(LOG_TRACE, "HttpRequest cleared");
}
