#include "webserv.hpp"

bool AHandler::process_location_redirect(Connection* conn) {
    const Location* location = conn->location_match_;

    // Check if this location has a redirect
    if (location->redirect_.empty()) {
        log(LOG_DEBUG, "process_location_redirect: No redirect configured for location %s", 
            location->path_.c_str());
        return false;  // No redirect
    }

    log(LOG_INFO, "REDIRECT: Location %s redirecting to %s", 
        location->path_.c_str(), location->redirect_.c_str());

    // Set up redirect response
    ErrorHandler::generate_error_response(conn, codes::MOVED_PERMANENTLY);
    conn->response_data_->headers_["Location"] = location->redirect_;
    return true;  // Redirect was processed
}

std::string AHandler::parse_absolute_path(Connection* conn) {
    
    const Location* request_location = conn->location_match_;
    std::string request_root = request_location->root_;
    const std::string& request_path = conn->request_data_->uri_;

    // --CHECK If the root starts with /, removed it
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
    log(LOG_DEBUG, "parse_absolute_path: Request root: %s, Relative path: %s, Absolute path: %s",
        request_root.c_str(), relative_path.c_str(), absolute_path.c_str());

    return (absolute_path);
}

bool AHandler::process_directory_redirect(Connection* conn,
                                                   std::string& absolute_path) {
    std::string uri = conn->request_data_->uri_;

    // Check if the request URI ends with a slash (indicating directory)
    bool uri_ends_with_slash = !uri.empty() && uri[uri.length() - 1] == '/';

    // Check if the absolute path is a directory
    struct stat path_stat;
    if (stat(absolute_path.c_str(), &path_stat) != 0 ||
        !S_ISDIR(path_stat.st_mode)) {
        // Not a directory or couldn't stat
        return false;
    }

    // If URI doesn't end with slash, redirect to add the slash (nginx behavior)
    if (!uri_ends_with_slash) {
        std::string query = conn->request_data_->query_string_;

        // Create redirect URL (same path + /)
        std::string redirect_url = uri + "/";

        // If there's a query string, append it
        if (!query.empty()) {
            redirect_url += "?" + query;
        }

        ErrorHandler::generate_error_response(conn, codes::MOVED_PERMANENTLY);
        conn->response_data_->headers_["Location"] = redirect_url;
        return true;
    }

    return false;  // No redirect, but path modified
}

// Handle directory path resolution (index file or autoindex)
bool AHandler::process_directory_index(Connection* conn,
                                                std::string& absolute_path,
                                                bool& need_autoindex) {
    // Ensure absolute_path ends with /
    if (absolute_path[absolute_path.length() - 1] != '/') {
        absolute_path += '/';
    }

    // Get location config
    const Location* location = conn->location_match_;
    std::string index = location->index_;

    if (!index.empty()) {
        std::string index_path = absolute_path + index;
        struct stat index_stat;
        // Check if index file exists and is a regular file
        if (stat(index_path.c_str(), &index_stat) == 0 &&
            S_ISREG(index_stat.st_mode)) {
            // Index file exists, update path to use it
            absolute_path = index_path;
            return true;  // Found and using index file
        }
    }

    // No index file found, check if autoindex is enabled
    if (location->autoindex_) {
        need_autoindex = true;
        return true;  // Will use autoindex
    }

    // No index file and autoindex is disabled
    // Fluxogram 403 - autoindex off - call error handler
    conn->response_data_->status_code_ = 403;
    conn->response_data_->status_message_ = "Forbidden";
    // conn->state_ = Connection::CONN_WRITING;
    return false;  // Error response set
}

void AHandler::generate_directory_listing(
    Connection* conn, const std::string& dir_path) {
    // Open the directory
    DIR* dir = opendir(dir_path.c_str());
    // Fluxogram 500 - request resource not found - call error handler
    if (!dir) {
        std::cerr << "Failed to open directory for listing: " << strerror(errno)
                  << std::endl;
        conn->response_data_->status_code_ = 500;
        conn->response_data_->status_message_ = "Internal Server Error";
        // conn->state_ = Connection::CONN_WRITING;
        return;
    }

    // Start building HTML for directory listing
    std::string html =
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>Index of " +
        conn->request_data_->uri_ +
        "</title>\n"
        "    <style>\n"
        "        body { font-family: Arial, sans-serif; margin: 0; padding: "
        "20px; color: #333; }\n"
        "        h1 { border-bottom: 1px solid #eee; padding-bottom: 10px; "
        "font-size: 24px; }\n"
        "        table { border-collapse: collapse; width: 100%; }\n"
        "        th { text-align: left; padding: 8px; border-bottom: 1px solid "
        "#ddd; color: #666; }\n"
        "        td { padding: 8px; border-bottom: 1px solid #eee; }\n"
        "        a { text-decoration: none; color: #0366d6; }\n"
        "        a:hover { text-decoration: underline; }\n"
        "        .name { width: 70%; }\n"
        "        .size { width: 30%; text-align: right; color: #666; }\n"
        "        .parent { margin-bottom: 10px; display: block; }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <h1>Index of " +
        conn->request_data_->uri_ + "</h1>\n";

    // Add parent directory link unless at root
    if (conn->request_data_->uri_ != "/") {
        html += "    <a class=\"parent\" href=\"../\">Parent Directory</a>\n";
    }

    html +=
        "    <table>\n"
        "        <tr>\n"
        "            <th class=\"name\">Name</th>\n"
        "            <th class=\"size\">Size</th>\n"
        "        </tr>\n";

    // Read and store directory entries
    struct dirent* entry;
    std::vector<std::string> dir_names;
    std::vector<std::string> file_names;

    while ((entry = readdir(dir)) != NULL) {
        std::string name = entry->d_name;

        // Skip "." and ".."
        if (name == "." || name == "..") {
            continue;
        }

        // Get file stats
        struct stat st;
        std::string full_path = dir_path + name;

        if (stat(full_path.c_str(), &st) == 0) {
            // Check if it's a directory
            if (S_ISDIR(st.st_mode)) {
                dir_names.push_back(name);
            } else {
                file_names.push_back(name);
            }
        }
    }

    // Sort directories and files alphabetically
    std::sort(dir_names.begin(), dir_names.end());
    std::sort(file_names.begin(), file_names.end());

    // Process directories first
    for (size_t i = 0; i < dir_names.size(); ++i) {
        std::string name = dir_names[i];

        html +=
            "        <tr>\n"
            "            <td class=\"name\"><a href=\"" +
            name + "/\">" + name +
            "/</a></td>\n"
            "            <td class=\"size\">-</td>\n"
            "        </tr>\n";
    }

    // Then process files
    for (size_t i = 0; i < file_names.size(); ++i) {
        std::string name = file_names[i];
        std::string full_path = dir_path + name;
        struct stat st;

        if (stat(full_path.c_str(), &st) != 0) {
            continue;  // Skip if stat fails
        }

        // Format file size
        std::string size_str;
        std::ostringstream oss;

        if (st.st_size < 1024) {
            oss << st.st_size << " B";
        } else if (st.st_size < 1024 * 1024) {
            oss << (st.st_size / 1024) << " KB";
        } else if (st.st_size < 1024 * 1024 * 1024) {
            oss << (st.st_size / (1024 * 1024)) << " MB";
        } else {
            oss << (st.st_size / (1024 * 1024 * 1024)) << " GB";
        }
        size_str = oss.str();

        html +=
            "        <tr>\n"
            "            <td class=\"name\"><a href=\"" +
            name + "\">" + name +
            "</a></td>\n"
            "            <td class=\"size\">" +
            size_str +
            "</td>\n"
            "        </tr>\n";
    }

    // Count total entries
    std::ostringstream count_str;
    count_str << (dir_names.size() + file_names.size());

    // Close HTML
    html +=
        "    </table>\n"
        "    <div style=\"margin-top: 20px; color: #666; font-size: 12px;\">\n"
        "        Webserv - " +
        count_str.str() +
        " items\n"
        "    </div>\n"
        "</body>\n"
        "</html>";

    // Set response
    // Fluxogram 200
    conn->response_data_->status_code_ = 200;
    conn->response_data_->status_message_ = "OK";
    conn->response_data_->headers_["Content-Type"] = "text/html";
    conn->response_data_->body_.assign(html.begin(), html.end());
    conn->conn_state_ = codes::CONN_WRITING;

    // Clean up
    closedir(dir);
}