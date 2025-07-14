#include "common.hpp"

const Location* RequestProcessor::match_location(
    const VirtualServer* vs, const std::string& path) const {
    log(LOG_TRACE,
        "RequestProcessor::match_location called for path '%s' on virtual "
        "server "
        "'%s:%d'",
        path.c_str(), vs->host_.c_str(), vs->port_);

    const Location* best_prefix = NULL;
    const Location* best_extension = NULL;

    for (size_t i = 0; i < vs->locations_.size(); ++i) {
        const Location& loc = vs->locations_[i];
        if (loc.type_ == LOC_EXTENSION) {
            // Extension match: path ends with loc.path_
            if (path.length() >= loc.path_.length() &&
                path.compare(path.length() - loc.path_.length(),
                             loc.path_.length(), loc.path_) == 0) {
                if (!best_extension ||
                    loc.path_.length() > best_extension->path_.length()) {
                    best_extension = &loc;
                }
            }
        } else {
            // Prefix match: path starts with loc.path_
            if (path.find(loc.path_) == 0 &&
                (loc.path_ == "/" || path == loc.path_ ||
                 (path.length() > loc.path_.length() &&
                  (path[loc.path_.length()] == '/' ||
                   loc.path_[loc.path_.length() - 1] == '/')))) {
                if (!best_prefix ||
                    loc.path_.length() > best_prefix->path_.length()) {
                    best_prefix = &loc;
                }
            }
        }
    }

    // Prefer extension match over prefix match if both exist
    if (best_extension) {
        log(LOG_DEBUG,
            "Matched extension location '%s' for path '%s' on virtual server "
            "'%s:%d'",
            best_extension->path_.c_str(), path.c_str(), vs->host_.c_str(),
            vs->port_);
        return best_extension;
    }
    if (best_prefix) {
        log(LOG_DEBUG,
            "Matched prefix location '%s' for path '%s' on virtual server "
            "'%s:%d'",
            best_prefix->path_.c_str(), path.c_str(), vs->host_.c_str(),
            vs->port_);
        return best_prefix;
    }

    // Should never reach this because we enforce a '/' location at parsing
    log(LOG_FATAL,
        "No matching location found for path '%s' on virtual server '%s:%d'",
        path.c_str(), vs->host_.c_str(), vs->port_);
    return NULL;
}

void RequestProcessor::match_host_header(
    Connection* conn, const std::map<int, std::vector<VirtualServer*> >&
                          listener_to_virtual_servers_) {
    if (!conn || !conn->request_data_ || !conn->default_virtual_server_) {
        log(LOG_FATAL, "match_host_header: Invalid connection or data.");
        return;
    }

    log(LOG_TRACE, "RequestProcessor::match_host_header called for client %d",
        conn->client_fd_);

    // Get Host header value from the request
    std::string request_host_header_val =
        conn->request_data_->get_header("Host");
    std::string target_hostname = request_host_header_val;
    if (target_hostname.empty()) {
        conn->virtual_server_ = conn->default_virtual_server_;
        log(LOG_DEBUG, "No Host header. Using default virtual server for %s:%d",
            conn->virtual_server_->host_.c_str(), conn->virtual_server_->port_);
        return;
    }

    // Strip port number from Host header if present
    size_t colon_pos = target_hostname.find(':');
    if (colon_pos != std::string::npos) {
        target_hostname = target_hostname.substr(0, colon_pos);
    }

    // Loop through all virtual servers for this listener
    std::map<int, std::vector<VirtualServer*> >::const_iterator map_it =
        listener_to_virtual_servers_.find(conn->client_fd_);

    if (map_it != listener_to_virtual_servers_.end()) {
        const std::vector<VirtualServer*>& vs_candidates = map_it->second;
        for (std::vector<VirtualServer*>::const_iterator it =
                 vs_candidates.begin();
             it != vs_candidates.end(); ++it) {
            if ((*it)->host_ == target_hostname) {
                conn->virtual_server_ = *it;
                log(LOG_DEBUG,
                    "Matched Host header '%s' to virtual server %s:%d",
                    target_hostname.c_str(), (*it)->host_.c_str(),
                    (*it)->port_);
                return;
            }
        }
    }

    // If no match found, use the default virtual server
    conn->virtual_server_ = conn->default_virtual_server_;
    log(LOG_DEBUG,
        "No match for Host header '%s'. Using default virtual server %s:%d",
        target_hostname.c_str(), conn->virtual_server_->host_.c_str(),
        conn->virtual_server_->port_);
}

bool RequestProcessor::handle_redirects(Connection* conn) {
    log(LOG_TRACE, "RequestProcessor::handle_redirects called for client %d",
        conn->client_fd_);

    // Check if the request path matches a redirect rule
    if (process_location_redirect(conn)) {
        log(LOG_DEBUG, "process_location_redirect: Redirected client %d to %s",
            conn->client_fd_,
            conn->response_data_->get_header("Location").c_str());
        return true;
    }

    // Check if the request path is a directory and needs to be redirected
    if (process_directory_redirect(conn, conn->request_data_->path_)) {
        log(LOG_DEBUG, "process_directory_redirect: Redirected client %d to %s",
            conn->client_fd_,
            conn->response_data_->get_header("Location").c_str());
        return true;
    }

    return false;
}

bool RequestProcessor::process_location_redirect(Connection* conn) {
    log(LOG_TRACE,
        "RequestProcessor::process_location_redirect called for client_fd %d",
        conn->client_fd_);

    const Location* location = conn->location_match_;

    // Check if this location has a redirect
    if (location->redirect_.empty()) {
        log(LOG_DEBUG,
            "process_location_redirect: No redirect configured for location %s",
            location->path_.c_str());
        return false;  // No redirect
    }

    log(LOG_INFO, "process_location_redirect: Location %s redirecting to %s",
        location->path_.c_str(), location->redirect_.c_str());

    // Set up redirect response
    conn->response_data_->set_header("Location", location->redirect_);
    conn->status_ = MOVED_PERMANENTLY;

    return true;
}

bool RequestProcessor::process_directory_redirect(Connection* conn,
                                                  std::string& absolute_path) {
    log(LOG_TRACE,
        "RequestProcessor::process_directory_redirect called for client_fd %d",
        conn->client_fd_);

    std::string path = conn->request_data_->path_;

    // Check if the request URI ends with a slash (indicating directory)
    bool path_ends_with_slash = !path.empty() && path[path.length() - 1] == '/';

    // Check if the absolute path is a directory
    struct stat path_stat;
    if (stat(absolute_path.c_str(), &path_stat) != 0 ||
        !S_ISDIR(path_stat.st_mode)) {
        // Not a directory or couldn't stat
        return false;
    }

    // If URI doesn't end with slash, redirect to add the slash (nginx behavior)
    if (!path_ends_with_slash) {
        std::string query = conn->request_data_->query_string_;

        // Create redirect URL (same path + /)
        std::string redirect_url = path + "/";

        // If there's a query string, append it
        if (!query.empty()) {
            redirect_url += "?" + query;
        }

        conn->response_data_->set_header("Location", redirect_url);
        conn->status_ = MOVED_PERMANENTLY;

        return true;
    }

    return false;
}

ParseStatus RequestProcessor::validate_version(Connection* conn) {
    log(LOG_TRACE, "RequestProcessor::validate_version called for client %d",
        conn->client_fd_);

    std::string version = conn->request_data_->version_;

    // Only HTTP/1.0 or HTTP/1.1 allowed
    if (version == "HTTP/1.0" || version == "HTTP/1.1") {
        return PARSE_SUCCESS;
    }

    log(LOG_ERROR, "Invalid HTTP version '%s' in request for connection: %d",
        version.c_str(), conn->client_fd_);
    return PARSE_VERSION_NOT_SUPPORTED;
}

ParseStatus RequestProcessor::validate_body_handling(Connection* conn) {
    log(LOG_TRACE,
        "RequestProcessor::validate_body_handling called for client %d",
        conn->client_fd_);

    HttpRequest* request = conn->request_data_;

    bool has_content_length = !request->get_header("content-length").empty();
    bool has_transfer_encoding =
        !request->get_header("transfer-encoding").empty();

    if (has_content_length && has_transfer_encoding) {
        log(LOG_ERROR,
            "POST/PUT with both Content-Length and Transfer-Encoding");
        return PARSE_INVALID_CONTENT_LENGTH;
    }

    if (request->method_ == "POST" || request->method_ == "PUT") {
        if (!has_content_length && !has_transfer_encoding) {
            log(LOG_ERROR,
                "POST/PUT without Content-Length or Transfer-Encoding");
            return PARSE_MISSING_CONTENT_LENGTH;
        }

        if (has_content_length) {
            std::string content_length = request->get_header("content-length");
            char* end_ptr;
            size_t body_size =
                std::strtoul(content_length.c_str(), &end_ptr, 10);

            if (end_ptr == content_length.c_str() || *end_ptr != '\0') {
                log(LOG_ERROR, "Invalid Content-Length header: '%s'",
                    content_length.c_str());
                return PARSE_INVALID_CONTENT_LENGTH;
            }

            if (static_cast<ssize_t>(body_size) >
                conn->location_match_->client_max_body_size_) {
                log(LOG_ERROR,
                    "Content-Length exceeds maximum size: %zu, limit: %zd",
                    body_size, conn->location_match_->client_max_body_size_);
                return PARSE_CONTENT_TOO_LARGE;
            }
        }

        if (has_transfer_encoding) {
            std::string transfer_encoding =
                request->get_header("transfer-encoding");
            if (transfer_encoding != "chunked") {
                log(LOG_ERROR, "Unknown Transfer-Encoding: '%s'",
                    transfer_encoding.c_str());
                return PARSE_UNKNOWN_ENCODING;
            }
        }
    }

    log(LOG_DEBUG, "Body handling validation successful for connection: %d",
        conn->client_fd_);
    return PARSE_SUCCESS;
}

ParseStatus RequestProcessor::validate_method_location_access(
    Connection* conn) {
    log(LOG_TRACE, "RequestProcessor::validate_method called for client %d",
        conn->client_fd_);

    std::string method = conn->request_data_->method_;
    if (method != "GET" && method != "POST" && method != "DELETE") {
        log(LOG_ERROR, "Invalid HTTP method '%s' in request for connection: %d",
            method.c_str(), conn->client_fd_);
        return PARSE_METHOD_NOT_IMPLEMENTED;
    }

    // Check if the method is allowed for the matched location
    const Location* location = conn->location_match_;
    for (std::vector<std::string>::const_iterator it =
             location->allowed_methods_.begin();
         it != location->allowed_methods_.end(); ++it) {
        if (*it == method) {
            return PARSE_SUCCESS;
        }
    }
    log(LOG_ERROR, "Method '%s' not allowed for location: %s", method.c_str(),
        location->path_.c_str());

    std::string allowed_methods_str = join(location->allowed_methods_, ", ");
    conn->response_data_->set_error_header("Allow", allowed_methods_str);
    return PARSE_METHOD_NOT_ALLOWED;
}
