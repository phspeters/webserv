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

    // TODO: Implement permission checks based on the request method

    return PARSE_SUCCESS;
}

Result StaticFileHandler::setup_handler(Connection* conn) {
    log(LOG_TRACE, "StaticFileHandler::setup_handler called for client_fd %d",
        conn->client_fd_);

    // TODO: implement handler setup

    return COMPLETE;
}

Result StaticFileHandler::handle(Connection* conn) {
    log(LOG_TRACE, "StaticFileHandler::handle called for client_fd %d",
        conn->client_fd_);

    // Parse and validate the absolute path
    std::string absolute_path;
    Result result = resolve_absolute_path(conn, absolute_path);
    if (result != COMPLETE) {
        return result;
    }

    // Handle directory requests (index files, autoindex, etc.)
    result = handle_directory_request(conn, absolute_path);
    if (result != COMPLETE) {
        return result;
    }

    // Validate file existence and permissions
    result = validate_file_access(conn, absolute_path);
    if (result != COMPLETE) {
        return result;
    }

    // Open file and prepare response
    return prepare_file_response(conn, absolute_path);
}

Result StaticFileHandler::resolve_absolute_path(Connection* conn,
                                                std::string& absolute_path) {
    log(LOG_TRACE,
        "StaticFileHandler::resolve_absolute_path called for client_fd %d",
        conn->client_fd_);

    absolute_path = parse_absolute_path(conn);
    if (absolute_path.empty()) {
        log(LOG_ERROR,
            "StaticFileHandler: Empty absolute path for client_fd %d",
            conn->client_fd_);
        conn->status_ = INTERNAL_SERVER_ERROR;
        return ERROR;
    }
    return COMPLETE;
}

Result StaticFileHandler::handle_directory_request(Connection* conn,
                                                   std::string& absolute_path) {
    log(LOG_TRACE,
        "StaticFileHandler::handle_directory_request called for client_fd %d",
        conn->client_fd_);

    if (absolute_path[absolute_path.length() - 1] != '/') {
        return COMPLETE;
    }

    bool need_autoindex = false;
    if (process_directory_index(conn, absolute_path, need_autoindex)) {
        if (need_autoindex) {
            generate_directory_listing(conn, absolute_path);
            return COMPLETE;
        }
    } else {
        conn->status_ = FORBIDDEN;
        return ERROR;
    }

    std::string index_file = conn->location_match_->index_;
    absolute_path = absolute_path + index_file;
    return COMPLETE;
}

Result StaticFileHandler::validate_file_access(
    Connection* conn, const std::string& absolute_path) {
    log(LOG_TRACE,
        "StaticFileHandler::validate_file_access called for client_fd %d",
        conn->client_fd_);

    struct stat file_info;
    if (stat(absolute_path.c_str(), &file_info) == -1) {
        if (errno == ENOENT || errno == ENOTDIR) {
            conn->status_ = NOT_FOUND;
        } else if (errno == EACCES) {
            conn->status_ = FORBIDDEN;
        } else {
            conn->status_ = INTERNAL_SERVER_ERROR;
        }
        return ERROR;
    }

    if (!S_ISREG(file_info.st_mode)) {
        conn->status_ = FORBIDDEN;
        return ERROR;
    }

    conn->static_file_context_->absolute_path_ = absolute_path;
    log(LOG_DEBUG,
        "StaticFileHandler: Permissions check passed for client_fd %d",
        conn->client_fd_);
    return COMPLETE;
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

    // Get file stats
    struct stat file_info;
    if (fstat(fd, &file_info) == -1) {
        close(fd);
        conn->status_ = INTERNAL_SERVER_ERROR;
        return ERROR;
    }

    conn->static_file_context_->file_fd_ = fd;

    set_response_headers(conn, file_info, absolute_path);

    log(LOG_INFO, "StaticFileHandler: File ready to be served for client_fd %d",
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

bool StaticFileHandler::process_directory_index(Connection* conn,
                                                std::string& absolute_path,
                                                bool& need_autoindex) {
    log(LOG_TRACE,
        "StaticFileHandler::process_directory_index called for client_fd %d",
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

bool StaticFileHandler::generate_directory_listing(
    Connection* conn, const std::string& dir_path) {
    log(LOG_TRACE,
        "StaticFileHandler::generate_directory_listing called for client_fd %d",
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

    conn->response_data_->status_code_ = OK;
    conn->response_data_->status_message_ = "OK";
    conn->response_data_->set_header("Content-Type", "text/html");
    std::ostringstream content_stream;
    content_stream << html.size();
    conn->response_data_->set_header("Content-Length", content_stream.str());
    conn->response_data_->body_data_.assign(html.begin(), html.end());

    return true;
}
