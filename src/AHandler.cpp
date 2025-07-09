#include "common.hpp"

std::string AHandler::parse_absolute_path(Connection* conn) {
    log(LOG_TRACE, "AHandler::parse_absolute_path called for client_fd %d",
        conn->client_fd_);

    const Location* request_location = conn->location_match_;
    std::string request_root = request_location->root_;
    const std::string& request_path = conn->request_data_->path_;

    // BIA: TALVEZ ISSO VOLTE!
    if (request_root[0] == '/') {
        request_root = request_root.substr(1);
    }

    // Calculate the path relative to the location
    std::string relative_path = "";

    // Calculate where the relative part starts
    size_t location_len = request_location->path_.length();

    // If location path ends with /, exclude it from length calculation
    if (!request_location->path_.empty() &&
        request_location->path_[location_len - 1] == '/') {
        location_len--;
    }

    // Extract the relative part (starting after the location path)
    if (request_path.length() > location_len) {
        relative_path = request_path.substr(location_len + 1);
        if (relative_path[0] != '/') {
            relative_path = "/" + relative_path;
        }
    }

    std::string absolute_path = request_root + relative_path;
    log(LOG_DEBUG,
        "parse_absolute_path: Request root: %s, Relative path: %s, Absolute "
        "path: %s",
        request_root.c_str(), relative_path.c_str(), absolute_path.c_str());

    return (absolute_path);
}
