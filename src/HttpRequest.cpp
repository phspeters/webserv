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
        return it->second;
    }
    return "";
}

void HttpRequest::set_header(const std::string& name, const std::string& value) {
	// Case-insensitive lookup for headers
	std::string lower_name = name;
	for (size_t i = 0; i < lower_name.size(); ++i) {
		lower_name[i] = std::tolower(static_cast<unsigned char>(lower_name[i]));
	}

	headers_[lower_name] = value;
}

void HttpRequest::clear() {
    method_.clear();
    uri_.clear();
    version_.clear();
    headers_.clear();
    body_.clear();
    path_.clear();
    query_string_.clear();
}
