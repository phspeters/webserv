#include "common.hpp"

std::string AHandler::parse_absolute_path(Connection* conn) {
    log(LOG_TRACE, "AHandler::parse_absolute_path called for client_fd %d",
        conn->client_fd_);

    const Location* location = conn->location_match_;
    std::string root_path = location->root_;
    const std::string& request_path = conn->request_data_->path_;

    if (!root_path.empty() && root_path[0] == '/') {
        root_path = root_path.substr(1);
    }

    // Filesystem Path = <root_path> + <request_uri>

    // To prevent issues like "path//file", we normalize the paths before joining.
    // 1. Remove trailing slash from root_path, if it exists.
    if (!root_path.empty() && root_path[root_path.length() - 1] == '/') {
        root_path.erase(root_path.length() - 1);
    }

    // 2. The request_path already includes the leading slash.
    std::string absolute_path = root_path + request_path;

    log(LOG_DEBUG,
        "parse_absolute_path: Root: '%s', Request Path: '%s', Absolute Path: '%s'",
        location->root_.c_str(), request_path.c_str(), absolute_path.c_str());

    return absolute_path;
}
