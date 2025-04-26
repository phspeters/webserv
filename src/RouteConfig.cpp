#include "webserv.hpp"

// --- RouteConfig Implementation ---

RouteConfig::RouteConfig()
    : directory_listing_(false), redirect_code_(0), uploads_enabled_(false) {
    // Defaults like path, root, index are often set based on context during
    // parsing
}

bool RouteConfig::is_method_allowed(const std::string& method) const {
    if (allowed_methods_.empty()) {
        // If no methods specified in this specific route, behavior might depend
        // on server-level defaults or HTTP standards (e.g., GET/HEAD often
        // assumed).
        return (method == "GET" || method == "HEAD");
    }
    return std::find(allowed_methods_.begin(), allowed_methods_.end(),
                     method) != allowed_methods_.end();
}