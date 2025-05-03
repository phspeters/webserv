#include "webserv.hpp"

HttpRequest::HttpRequest() : parse_status_(RequestParser::PARSE_INCOMPLETE) {}

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
        return it->second;
    }
    return "";
}

bool HttpRequest::is_valid() const {
    return parse_status_ == RequestParser::PARSE_SUCCESS;
}

void HttpRequest::clear() {
    method_.clear();
    uri_.clear();
    version_.clear();
    headers_.clear();
    body_.clear();
    path_.clear();
    query_string_.clear();
    parse_status_ = RequestParser::PARSE_INCOMPLETE;
}
