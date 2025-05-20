#include "webserv.hpp"

StaticFileHandler::StaticFileHandler() {}

StaticFileHandler::~StaticFileHandler() {}

void StaticFileHandler::handle(Connection* conn) {
    //--CHECK Check if the request has errors to return an error response

    // Fluxogram 301 - when location has redirect
    if (process_location_redirect(conn)) {
        return;  // Redirect response was set up, we're done
    }

    // Fluxogram 405 - call error handler
    // Check if the request method is allowed (only supporting GET) // Fluxogram
    // OK
    if (conn->request_data_->method_ != "GET") {
        conn->response_data_->status_code_ = 405;
        conn->response_data_->status_message_ = "Method Not Allowed";
        conn->response_data_->headers_["Allow"] = "GET";
        // conn->state_ = Connection::CONN_WRITING;
        return;
    }

    std::string absolute_path = parse_absolute_path(conn->request_data_);

    // Fluxogram 301 - check if the request should be a directory
    if (process_directory_redirect(conn, absolute_path)) {
        return;  // Redirect was set up, we're done
    }

    bool need_autoindex = false;
    if (process_directory_index(conn, absolute_path, need_autoindex)) {
        if (need_autoindex) {
            generate_directory_listing(conn, absolute_path);
            return;
        }
    }

    // Try to open the file
    int fd = open(absolute_path.c_str(), O_RDONLY);
    if (fd == -1) {
        // Fluxogram 404 - request resource not found - call error handler
        if (errno == ENOENT) {
            // File not found
            conn->response_data_->status_code_ = 404;
            conn->response_data_->status_message_ = "Not Found";
            // Not in the Fluxogram, but possible 403 - call error handler
        } else if (errno == EACCES) {
            // Permission denied
            conn->response_data_->status_code_ = 403;
            conn->response_data_->status_message_ = "Forbidden";
            // Not in the Fluxogram, but possible 500 - call error handler
        } else {
            // Other error
            conn->response_data_->status_code_ = 500;
            conn->response_data_->status_message_ = "Internal Server Error";
        }
        // conn->state_ = Connection::CONN_WRITING;
        return;
    }

    // Get file info
    // Not in the Fluxogram, but possible 500 - call error handler
    struct stat file_info;
    if (fstat(fd, &file_info) == -1) {
        close(fd);
        conn->response_data_->status_code_ = 500;
        conn->response_data_->status_message_ = "Internal Server Error";
        // conn->state_ = Connection::CONN_WRITING;
        return;
    }

    // Check if it's a regular file
    // Not in the Fluxogram, but possible 403 - call error handler
    if (!S_ISREG(file_info.st_mode)) {
        close(fd);
        conn->response_data_->status_code_ = 403;
        conn->response_data_->status_message_ = "Forbidden";
        // conn->state_ = Connection::CONN_WRITING;
        return;
    }

    // Determine content type
    std::string content_type = "application/octet-stream";  // Default type
    size_t dot_pos = absolute_path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        std::string extension = absolute_path.substr(dot_pos + 1);

        // Map common extensions to MIME types
        if (extension == "html" || extension == "htm") {
            content_type = "text/html";
        } else if (extension == "css") {
            content_type = "text/css";
        } else if (extension == "js") {
            content_type = "application/javascript";
        } else if (extension == "jpg" || extension == "jpeg") {
            content_type = "image/jpeg";
        } else if (extension == "png") {
            content_type = "image/png";
        } else if (extension == "gif") {
            content_type = "image/gif";
        } else if (extension == "txt") {
            content_type = "text/plain";
        }
    }

    // Read the file content
    std::vector<char> file_content(file_info.st_size);
    ssize_t bytes_read = read(fd, &file_content[0], file_info.st_size);
    close(fd);

    // Not in the Fluxogram, but possible 500 - call error handler
    if (bytes_read != file_info.st_size) {
        conn->response_data_->status_code_ = 500;
        conn->response_data_->status_message_ = "Internal Server Error";
        // conn->state_ = Connection::CONN_WRITING;
        return;
    }

    // Prepare response headers
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = content_type;

    // Convert file size to string using ostringstream (C++98 compatible)
    std::ostringstream size_stream;
    size_stream << file_info.st_size;
    headers["Content-Length"] = size_stream.str();

    // Prepare the response
    // Fluxogram 200
    conn->response_data_->status_code_ = 200;
    conn->response_data_->status_message_ = "OK";
    conn->response_data_->headers_ = headers;
    conn->response_data_->body_.assign(file_content.begin(),
                                       file_content.end());

    // Update connection state
    // conn->state_ = Connection::CONN_WRITING;
}

bool StaticFileHandler::process_directory_redirect(Connection* conn,
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

        conn->response_data_->status_code_ = 301;
        conn->response_data_->status_message_ = "Moved Permanently";
        conn->response_data_->headers_["Location"] = redirect_url;
        // conn->state_ = Connection::CONN_WRITING;
        return true;
    }

    return false;  // No redirect, but path modified
}

// Handle directory path resolution (index file or autoindex)
bool StaticFileHandler::process_directory_index(Connection* conn,
                                                std::string& absolute_path,
                                                bool& need_autoindex) {
    // Ensure absolute_path ends with /
    if (absolute_path[absolute_path.length() - 1] != '/') {
        absolute_path += '/';
    }

    // Get location config
    const Location* location = conn->request_data_->location_match_;
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

void StaticFileHandler::generate_directory_listing(
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
    // conn->state_ = Connection::CONN_WRITING;

    // Clean up
    closedir(dir);
}