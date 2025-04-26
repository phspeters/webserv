#include "webserv.hpp"

// --- ServerBlock Implementation ---

ServerBlock::ServerBlock()
    : host_("0.0.0.0"),              // Default listen address
      port_(80),                     // Default HTTP port
      max_body_size_(1024 * 1024) {  // 1MB default
}

std::string ServerBlock::get_error_page(int status_code) const {
    std::map<int, std::string>::const_iterator it =
        error_pages_.find(status_code);
    if (it != error_pages_.end()) {
        return it->second;
    }
    return "";  // No custom error page defined for this code in this block
}