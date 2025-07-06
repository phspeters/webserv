#include "common.hpp"

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, last - first + 1);
}

std::string get_status_message(int code) {
    switch (code) {
        // 2xx - Success
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 206:
            return "Partial Content";

        // 3xx - Redirection
        case 301:
            return "Moved Permanently";
        case 302:
            return "Found";
        case 303:
            return "See Other";
        case 304:
            return "Not Modified";
        case 307:
            return "Temporary Redirect";
        case 308:
            return "Permanent Redirect";

        // 4xx - Client Error
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 406:
            return "Not Acceptable";
        case 408:
            return "Request Timeout";
        case 409:
            return "Conflict";
        case 413:
            return "Payload Too Large";
        case 414:
            return "URI Too Long";
        case 415:
            return "Unsupported Media Type";
        case 429:
            return "Too Many Requests";
        case 431:
            return "Request Header Fields Too Large";

        // 5xx - Server Error
        case 500:
            return "Internal Server Error";
        case 501:
            return "Not Implemented";
        case 502:
            return "Bad Gateway";
        case 503:
            return "Service Unavailable";
        case 504:
            return "Gateway Timeout";
        case 505:
            return "HTTP Version Not Supported";

        default:
            return "Unknown Status";
    }
}

std::string get_current_gmt_time() {
    char buffer[100];
    time_t now = time(NULL);
    struct tm* tm_info = gmtime(&now);

    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S: ", tm_info);
    return std::string(buffer);
}

bool is_cgi_extension(const std::string& request_uri) {
    std::string extension = get_file_extension(request_uri);
    if (!extension.empty() && (extension == ".php" || extension == ".py" ||
                               extension == ".sh" || extension == ".bla")) {
        log(LOG_DEBUG, "Request uri: '%s' is a CGI script",
            request_uri.c_str());
        return true;
    }
    return false;
}

std::string get_file_extension(const std::string& uri_path) {
    size_t dot_pos = uri_path.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return "";
    }
    std::string extension = uri_path.substr(dot_pos);
    // Convert to lowercase for case-insensitive comparison
    for (std::string::iterator it = extension.begin(); it != extension.end();
         ++it) {
        *it = std::tolower(*it);
    }
    return extension;
}

std::string join(const std::vector<std::string>& vec, const std::string& sep) {
    std::ostringstream oss;
    for (size_t i = 0; i < vec.size(); ++i) {
        if (i != 0) oss << sep;
        oss << vec[i];
    }
    return oss.str();
}

bool set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        log(LOG_ERROR, "Failed to get flags for socket '%i'", fd);
        return false;
    }

    flags |= O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) == -1) {
        log(LOG_ERROR, "Failed to set non-blocking mode for socket '%i'", fd);
        return false;
    }

    return true;
}

// It checks if a character is a valid "tchar" according to RFC 7230.
bool is_token_char(char c) {
    // Check for alphanumeric characters
    if (isalnum(c)) {
        return true;
    }
    // Check against the list of allowed special characters.
    return strchr("!#$%&'*+-.^_`|~", c) != NULL;
}

bool is_line_ending_char(char c) { return c == '\r' || c == '\n'; }
