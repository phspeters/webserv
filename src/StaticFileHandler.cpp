#include "common.hpp"

StaticFileHandler::StaticFileHandler() {}

StaticFileHandler::~StaticFileHandler() {}

Result StaticFileHandler::initialize_context(Connection* conn) {
    log(LOG_TRACE,
        "StaticFileHandler::initialize_context called for client_fd %d",
        conn->client_fd_);

    try {
        conn->static_file_context_ = new StaticFileContext();
    } catch (const std::bad_alloc& e) {
        log(LOG_ERROR,
            "StaticFileHandler::initialize_context: Memory allocation failed "
            "for client_fd %d",
            conn->client_fd_);
        conn->status_ = INTERNAL_SERVER_ERROR;
        return ERROR;
    }

    return COMPLETE;
}

ParseStatus StaticFileHandler::check_permissions(Connection* conn) {
    log(LOG_TRACE,
        "StaticFileHandler::check_permissions called for client_fd %d",
        conn->client_fd_);

    // Parse and validate the absolute path
    std::string absolute_path;
    ParseStatus status = resolve_absolute_path(conn, absolute_path);
    if (status != PARSE_SUCCESS) {
        return status;
    }

    // Handle directory requests (index files, autoindex, etc.)
    status = handle_directory_request(conn, absolute_path);
    if (status != PARSE_SUCCESS) {
        return status;
    }

    // Validate file existence and permissions
    status = validate_file_access(conn, absolute_path);
    if (status != PARSE_SUCCESS) {
        return status;
    }

    return PARSE_SUCCESS;
}

Result StaticFileHandler::setup_handler(Connection* conn) {
    log(LOG_TRACE, "StaticFileHandler::setup_handler called for client_fd %d",
        conn->client_fd_);

    return prepare_file_response(conn, conn->static_file_context_->absolute_path_);
}

Result StaticFileHandler::handle(Connection* conn) {
    log(LOG_TRACE, "StaticFileHandler::handle called for client_fd %d",
        conn->client_fd_);

    // Get file stats
    struct stat file_info;
    if (fstat(conn->static_file_context_->file_fd_, &file_info) == -1) {
        close(conn->static_file_context_->file_fd_);
        conn->status_ = INTERNAL_SERVER_ERROR;
        return ERROR;
    }

    set_response_headers(conn, file_info, conn->static_file_context_->absolute_path_);
    log(LOG_INFO, "StaticFileHandler: File prepared to be served for client_fd %d",
        conn->client_fd_);
    return COMPLETE;
}   

ParseStatus StaticFileHandler::resolve_absolute_path(Connection* conn,
                                                std::string& absolute_path) {
    log(LOG_TRACE,
        "StaticFileHandler::resolve_absolute_path called for client_fd %d",
        conn->client_fd_);

    absolute_path = parse_absolute_path(conn);
    if (absolute_path.empty()) {
        log(LOG_ERROR,
            "StaticFileHandler: Empty absolute path for client_fd %d",
            conn->client_fd_);
        // conn->status_ = INTERNAL_SERVER_ERROR;
        return PARSE_INTERNAL_ERROR;
    }
    return PARSE_SUCCESS;
}

ParseStatus StaticFileHandler::handle_directory_request(Connection* conn,
                                                   std::string& absolute_path) {
    log(LOG_TRACE,
        "StaticFileHandler::handle_directory_request called for path: %s",
        absolute_path.c_str());

    // 1. Check if the path is actually a directory. If not, do nothing.
    struct stat path_stat;
    if (stat(absolute_path.c_str(), &path_stat) != 0 || !S_ISDIR(path_stat.st_mode)) {
        return PARSE_SUCCESS; // Not a directory, so the main handler will treat it as a file.
    }

    // 2. HIGHEST PRIORITY: Handle redirect for missing trailing slash.
    if (handle_missing_slash_redirect(conn)) {
        return PARSE_MOVED_PERMANENTLY; // A 301 redirect response was prepared. We are done.
    }

    // 3. SECOND PRIORITY: Try to find and serve an index file.
    std::string original_path = absolute_path; // Keep for autoindex if needed
    if (resolve_index_file(conn, absolute_path)) {
        // Index file was found and absolute_path was modified to point to it.
        // Return COMPLETE to let the main handler serve it as a regular file.
        log(LOG_DEBUG, "Directory request resolved to index file: %s", absolute_path.c_str());
        return PARSE_SUCCESS;
    }

    // CHECK: how does it now if the response are ready to be sent? Can not return PARSE_SUCCESS
    // 4. THIRD PRIORITY: Check if autoindex is on.
    if (conn->location_match_->autoindex_) {
        return generate_directory_listing(conn, original_path); // A 200 OK response with the listing can be prepared. We are done.
    }

    // 5. FINAL FALLBACK: No index file and autoindex is off.
    log(LOG_DEBUG, "Directory request for '%s' is forbidden (no index, autoindex off)",
        original_path.c_str());
    // conn->status_ = FORBIDDEN;
    return PARSE_FORBIDDEN;
}

ParseStatus StaticFileHandler::validate_file_access(
    Connection* conn, const std::string& absolute_path) {
    log(LOG_TRACE,
        "StaticFileHandler::validate_file_access called for client_fd %d",
        conn->client_fd_);

    log(LOG_ERROR, "absolute_path = %s", absolute_path.c_str());

    struct stat file_info;
    if (stat(absolute_path.c_str(), &file_info) == -1) {
        if (errno == ENOENT || errno == ENOTDIR) {
            // conn->status_ = NOT_FOUND;
            return PARSE_NOT_FOUND;
        } else if (errno == EACCES) {
            // conn->status_ = FORBIDDEN;
            return PARSE_FORBIDDEN;
        } else {
            // conn->status_ = INTERNAL_SERVER_ERROR;
            return PARSE_INTERNAL_ERROR;
        }
    }

    if (!S_ISREG(file_info.st_mode)) {
        // conn->status_ = FORBIDDEN;
        return PARSE_FORBIDDEN;
    }

    conn->static_file_context_->absolute_path_ = absolute_path;
    log(LOG_DEBUG,
        "StaticFileHandler: Permissions check passed for client_fd %d",
        conn->client_fd_);
    return PARSE_SUCCESS;
}

bool StaticFileHandler::handle_missing_slash_redirect(Connection* conn) {
    // Check if the request URI is missing a trailing slash.
    const std::string& path = conn->request_data_->path_;
    bool path_ends_with_slash = !path.empty() && path[path.length() - 1] == '/';

    if (path_ends_with_slash) {
        return false; // Has a slash, no redirect needed.
    }

    // If URI doesn't end with slash, redirect to add the slash.
    std::string query = conn->request_data_->query_string_;
    std::string redirect_url = path + "/";
    if (!query.empty()) {
        redirect_url += "?" + query;
    }

    log(LOG_DEBUG, "handle_missing_slash_redirect: Redirecting to %s for client_fd %d",
        redirect_url.c_str(), conn->client_fd_);
    
    conn->status_ = MOVED_PERMANENTLY;
    conn->response_data_->status_code_ = 301;
    conn->response_data_->status_message_ = "Moved Permanently";
    conn->response_data_->set_header("Location", redirect_url);
    conn->status_ = MOVED_PERMANENTLY;

    return true;
}

bool StaticFileHandler::resolve_index_file(Connection* conn, std::string& absolute_path) {
    const Location* location = conn->location_match_;
    if (!location || location->index_.empty()) {
        return false; // No index file configured for this location.
    }

    std::string index_path = absolute_path + location->index_;
    struct stat index_stat;

    // Check if the index file exists and is a regular file.
    if (stat(index_path.c_str(), &index_stat) == 0 && S_ISREG(index_stat.st_mode)) {
        // Index file found! Modify the absolute_path to point to it.
        absolute_path = index_path;
        return true;
    }

    return false; // Index file not found or is not a regular file.
}

ParseStatus StaticFileHandler::generate_directory_listing(
    Connection* conn, const std::string& dir_path) {
    log(LOG_TRACE,
        "StaticFileHandler::generate_directory_listing called for client_fd %d",
        conn->client_fd_);

    DIR* dir = opendir(dir_path.c_str());
    if (!dir) {
        log(LOG_ERROR, "Failed to open directory for listing: %s",
            strerror(errno));
        // conn->status_ = INTERNAL_SERVER_ERROR;
        return PARSE_INTERNAL_ERROR;
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


    // CHECK: how does it now if the response are ready to be sent? Can not return PARSE_SUCCESS

    conn->response_data_->headers_.clear();
    conn->response_data_->body_data_.clear();

    conn->response_data_->status_code_ = OK; 
    conn->response_data_->status_message_ = "OK";
    conn->response_data_->set_header("Content-Type", "text/html");
    std::ostringstream content_stream;
    content_stream << html.size();
    conn->response_data_->set_header("Content-Length", content_stream.str());
    conn->response_data_->body_data_.assign(html.begin(), html.end());

    return PARSE_SUCCESS;
}

Result StaticFileHandler::prepare_file_response(
    Connection* conn, const std::string& absolute_path) {
    log(LOG_TRACE,
        "StaticFileHandler::prepare_file_response called for client_fd %d",
        conn->client_fd_);

    int fd = open(absolute_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd == -1) {
        return handle_file_open_error(conn);
    }

    // Moved to handle method
    // Get file stats
    // struct stat file_info;
    // if (fstat(fd, &file_info) == -1) {
    //     close(fd);
    //     conn->status_ = INTERNAL_SERVER_ERROR;
    //     return ERROR;
    // }

    conn->static_file_context_->file_fd_ = fd;

    // Moved to handle method
    // set_response_headers(conn, file_info, absolute_path);

    log(LOG_INFO, "StaticFileHandler: File prepared to be read for client_fd %d",
        conn->client_fd_);
    return COMPLETE;
}

Result StaticFileHandler::handle_file_open_error(Connection* conn) {
    if (errno == ENOENT) {
        conn->status_ = NOT_FOUND;
    } else if (errno == EACCES) {
        conn->status_ = FORBIDDEN;
    } else {
        conn->status_ = INTERNAL_SERVER_ERROR;
    }
    return ERROR;
}

void StaticFileHandler::set_response_headers(Connection* conn,
                                             const struct stat& file_info,
                                             const std::string& absolute_path) {
    log(LOG_TRACE,
        "StaticFileHandler::set_response_headers called for client_fd %d",
        conn->client_fd_);

    // CHECK: ensure request_data_->path_ also works
    std::string content_type = determine_content_type(absolute_path);

    conn->response_data_->status_code_ = 200;
    conn->response_data_->status_message_ = "OK";
    conn->response_data_->set_header("Content-Type", content_type);

    std::ostringstream size_stream;
    size_stream << file_info.st_size;
    conn->response_data_->set_header("Content-Length", size_stream.str());

    conn->response_data_->body_fd_ = conn->static_file_context_->file_fd_;
}

std::string StaticFileHandler::determine_content_type(const std::string& path) {
    log(LOG_TRACE,
        "StaticFileHandler::determine_content_type called with path: %s",
        path.c_str());

    size_t dot_pos = path.find_last_of('.');
    if (dot_pos == std::string::npos) {
        log(LOG_WARNING, "StaticFileHandler: No extension found for path: %s",
            path.c_str());
        return "application/octet-stream";
    }

    std::string ext = path.substr(dot_pos + 1);
    if (ext == "html" || ext == "htm") {
        return "text/html";
    } else if (ext == "css") {
        return "text/css";
    } else if (ext == "js") {
        return "application/javascript";
    } else if (ext == "png") {
        return "image/png";
    } else if (ext == "jpg" || ext == "jpeg") {
        return "image/jpeg";
    } else if (ext == "gif") {
        return "image/gif";
    } else if (ext == "txt") {
        return "text/plain";
    } else {
        return "application/octet-stream";
    }
}

void StaticFileHandler::cleanup_handler(Connection* conn) {
    if (!conn) {
        log(LOG_FATAL,
            "StaticFileHandler: Cleanup called with NULL connection");
        return;
    }

    log(LOG_DEBUG, "StaticFileHandler: Cleaning up handler for client_fd %d",
        conn->client_fd_);

    //  Close the file descriptor
    if (conn->static_file_context_->file_fd_ >= 0) {
        close(conn->static_file_context_->file_fd_);
        conn->static_file_context_->file_fd_ = -1;
    }

    // Delete the context object
    if (conn->static_file_context_) {
        delete conn->static_file_context_;
        conn->static_file_context_ = NULL;
    }
}