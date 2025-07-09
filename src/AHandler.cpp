#include "common.hpp"

bool AHandler::process_location_redirect(Connection* conn) {
    log(LOG_TRACE, "AHandler::process_location_redirect called for client_fd %d",
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

bool AHandler::process_directory_redirect(Connection* conn,
                                          std::string& absolute_path) {
    log(LOG_TRACE, "AHandler::process_directory_redirect called for client_fd %d",
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

bool AHandler::process_directory_index(Connection* conn,
                                       std::string& absolute_path,
                                       bool& need_autoindex) {
    log(LOG_TRACE, "AHandler::process_directory_index called for client_fd %d",
        conn->client_fd_);

    // Get location config
    const Location* location = conn->location_match_;
    std::string index = location->index_;
    if (!index.empty()) {
        std::string index_path = absolute_path + index;
        struct stat index_stat;
        log(LOG_DEBUG, "process_directory_index: Checking for index file at %s",
            index_path.c_str());
        // Check if index file exists and is a regular file
        if (stat(index_path.c_str(), &index_stat) == 0 &&
            S_ISREG(index_stat.st_mode)) {
            return true;  // Found and using index file
        }
    }

    // No index file found, check if autoindex is enabled
    if (location->autoindex_) {
        need_autoindex = true;
        log(LOG_DEBUG,
            "process_directory_index: No index file found, autoindex enabled "
            "for %s",
            absolute_path.c_str());
        return true;  // Will use autoindex
    }

    log(LOG_DEBUG,
        "process_directory_index: No index file and autoindex disabled for %s",
        absolute_path.c_str());
    return false;
}

bool AHandler::generate_directory_listing(Connection* conn,
                                          const std::string& dir_path) {
    log(LOG_TRACE, "AHandler::generate_directory_listing called for client_fd %d",
        conn->client_fd_);

    DIR* dir = opendir(dir_path.c_str());
    if (!dir) {
        log(LOG_ERROR, "Failed to open directory for listing: %s",
            strerror(errno));
        conn->status_ = INTERNAL_SERVER_ERROR;
        return false;
    }

    // Minimalist HTML structure, similar to Nginx
    std::string html = "<html>\n<head><title>Index of " +
                       conn->request_data_->path_ +
                       "</title></head>\n<body>\n<h1>Index of " +
                       conn->request_data_->path_ + "</h1><hr><pre>";

    // Add parent directory link
    html += "<a href=\"../\">../</a>\n";

    // Read and store directory entries to sort them later
    std::vector<std::pair<std::string, bool> >
        entries;  // Pair: <name, is_directory>
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        std::string full_path = dir_path + name;
        struct stat st;
        if (stat(full_path.c_str(), &st) == 0) {
            entries.push_back(std::make_pair(name, S_ISDIR(st.st_mode)));
        }
    }
    closedir(dir);

    // Sort entries alphabetically
    std::sort(entries.begin(), entries.end());

    // Add sorted entries to the HTML
    for (size_t i = 0; i < entries.size(); ++i) {
        std::string name = entries[i].first;
        bool is_dir = entries[i].second;
        if (is_dir) {
            name += "/";
        }
        html += "<a href=\"" + name + "\">" + name + "</a>\n";
    }

    html += "</pre><hr></body>\n</html>";

    // Set the response data for a 200 OK status
    log(LOG_DEBUG,
        "generate_directory_listing: Generated directory listing for %s",
        dir_path.c_str());

    conn->response_data_->headers_.clear();
    conn->response_data_->body_data_.clear();

    conn->response_data_->status_code_ = 200;
    conn->response_data_->status_message_ = "OK";
    conn->response_data_->set_header("Content-Type", "text/html");
    std::ostringstream content_stream;
    content_stream << html.size();
    conn->response_data_->set_header("Content-Length", content_stream.str());
    conn->response_data_->body_data_.assign(html.begin(), html.end());
    conn->status_ = OK;

    return true;
}
