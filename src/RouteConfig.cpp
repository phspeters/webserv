#include "RouteConfig.hpp"

#include <algorithm>

// --- RouteConfig Implementation ---

RouteConfig::RouteConfig()
    : directory_listing(false), redirect_code(0), uploads_enabled(false) {
    // Defaults like path, root, index are often set based on context during
    // parsing
}

bool RouteConfig::isMethodAllowed(const std::string& method) const {
    if (allowed_methods.empty()) {
        // If no methods specified in this specific route, behavior might depend
        // on server-level defaults or HTTP standards (e.g., GET/HEAD often
        // assumed).
        return (method == "GET" || method == "HEAD");
    }
    return std::find(allowed_methods.begin(), allowed_methods.end(), method) !=
           allowed_methods.end();
}