#include "ServerBlock.hpp"
#include <map>
#include <string>

// --- ServerBlock Implementation ---

ServerBlock::ServerBlock() :
    host("0.0.0.0"),      // Default listen address
    port(80),             // Default HTTP port
    max_body_size(1024 * 1024) { // 1MB default
}

std::string ServerBlock::getErrorPage(int status_code) const {
    std::map<int, std::string>::const_iterator it = error_pages.find(status_code);
    if (it != error_pages.end()) {
        return it->second;
    }
    return ""; // No custom error page defined for this code in this block
}