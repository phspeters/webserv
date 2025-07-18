#include "common.hpp"

std::string AHandler::parse_absolute_path(Connection* conn) {
    log(LOG_TRACE, "AHandler::parse_absolute_path called for client_fd %d",
        conn->client_fd_);

    const Location* request_location = conn->location_match_;
    std::string request_root = request_location->root_;
    const std::string& request_path = conn->request_data_->path_;
    const std::string& location_path = request_location->path_;

    if (request_root[0] == '/') {
        request_root = request_root.substr(1);
    }
    if (!request_root.empty() &&
        request_root[request_root.length() - 1] == '/') {
        request_root.erase(request_root.length() - 1);
    }
    std::string relative_path;
    if (request_path.length() >= location_path.length()) {
        relative_path = request_path.substr(location_path.length());
    }
    std::string absolute_path = request_root + "/" + relative_path;

    log(LOG_DEBUG,
        "parse_absolute_path: Request root: %s, Request path: %s, Absolute "
        "path: %s",
        request_root.c_str(), request_path.c_str(), absolute_path.c_str());

    return absolute_path;
}
