#include "webserv.hpp"

// Server defaults
static const int DEFAULT_PORT = 80;
static const std::string DEFAULT_HOST = "0.0.0.0";
static const size_t DEFAULT_MAX_BODY_SIZE = 1024 * 1024;  // 1MB
static const std::string DEFAULT_SERVER_NAME = "default_server";

// Error page defaults
static const int DEFAULT_404_ERROR_CODE = 404;
static const std::string DEFAULT_404_ERROR_PAGE = "/error/404.html";
static const int DEFAULT_500_ERROR_CODE = 500;
static const std::string DEFAULT_500_ERROR_PAGE = "/error/500.html";

// Location defaults
static const bool DEFAULT_AUTOINDEX = false;
static const bool DEFAULT_CGI_ENABLED = false;
static const std::string DEFAULT_INDEX = "index.html";

static std::vector<std::string> create_default_allowed_methods() {
    std::vector<std::string> methods;
    methods.push_back("GET");
    methods.push_back("POST");
    methods.push_back("DELETE");
    return methods;
}

static const std::vector<std::string> DEFAULT_ALLOWED_METHODS =
    create_default_allowed_methods();

// Constructor for Location with defaults
Location::Location()
    : autoindex_(DEFAULT_AUTOINDEX),
      cgi_enabled_(DEFAULT_CGI_ENABLED),
      index_(DEFAULT_INDEX) {
    allowed_methods_ = DEFAULT_ALLOWED_METHODS;
}

// Default constructor implementation
VirtualServer::VirtualServer()
    : port_(DEFAULT_PORT),
      listen_specified_(false),
      client_max_body_size_(DEFAULT_MAX_BODY_SIZE) {
    host_ = DEFAULT_HOST;
}

bool VirtualServer::parse_server_block(std::ifstream& file,
                                       VirtualServer& virtual_server) {
    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);

        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Check for block end
        if (line == "}") {
            virtual_server.apply_defaults();
            return true;
        }

        if (line.find("location") == 0) {
            if (!parse_location_block(file, line, virtual_server)) {
                return false;
            }
            continue;
        }

        std::string key, value;
        if (parse_directive(line, key, value)) {
            if (!handle_server_directive(key, value, virtual_server)) {
                return false;
            }
        } else {
            log(LOG_ERROR, "Invalid directive in server block: %s", line.c_str());
            return false;
        }
    }

    // Reached end of file without closing brace
    return false;
}

bool VirtualServer::parse_location_block(std::ifstream& file, std::string line,
                                         VirtualServer& virtual_server) {
    // Extract location path
    size_t pathStart = line.find_first_not_of(" \t", 8);  // Skip "location"
    if (pathStart == std::string::npos) return false;

    size_t pathEnd = line.find_first_of(" \t{", pathStart);
    if (pathEnd == std::string::npos) return false;

    std::string path = line.substr(pathStart, pathEnd - pathStart);

    // Find opening brace
    size_t bracePos = line.find('{', pathEnd);
    if (bracePos == std::string::npos) {
        // Get next line and look for brace
        std::string nextLine;
        if (!std::getline(file, nextLine)) return false;

        nextLine = trim(nextLine);
        if (nextLine != "{") return false;
    }

    // Create a new Location
    Location location;
    location.path_ = path;

    // Parse location block
    std::string locLine;
    while (std::getline(file, locLine)) {
        locLine = trim(locLine);

        if (locLine.empty() || locLine[0] == '#') {
            continue;
        }

        if (locLine == "}") {
            virtual_server.locations_.push_back(location);
            return true;
        }

        std::string key, value;
        if (parse_directive(locLine, key, value)) {
            if (!add_directive_value(location, key, value)) {
                return false;  // Reject if directive isn't recognized
            }
        }
    }

    return false;
}

// Handle server directives with a separate method
bool VirtualServer::handle_server_directive(const std::string& key,
                                            const std::string& value,
                                            VirtualServer& virtual_server) {
    if (key == "listen") {
        virtual_server.listen_specified_ = true;
        return parse_listen(value, virtual_server);
    } else if (key == "server_name") {
        return parse_server_name(value, virtual_server);
    } else if (key == "error_page") {
        return parse_error_page(value, virtual_server);
    } else if (key == "client_max_body_size") {
        return parse_client_max_body_size(value, virtual_server);
    } else {
        log(LOG_ERROR, "Unknown directive in server block: %s", key.c_str());
        return false;
    }
}

bool VirtualServer::parse_listen(const std::string& value,
                                 VirtualServer& virtual_server) {
    // Extract hostname/IP and port from the value
    std::string host_str;
    size_t colonPos = value.find(':');

    if (colonPos != std::string::npos) {
        // Format: hostname:port or ip:port
        host_str = value.substr(0, colonPos);
        std::istringstream iss(value.substr(colonPos + 1));
        if (!(iss >> virtual_server.port_)) {
            log(LOG_ERROR, "Invalid listen directive format: %s", value.c_str());
            return false;
        }
    } else {
        // Just a port number
        std::istringstream iss(value);
        if (!(iss >> virtual_server.port_)) {
            log(LOG_ERROR, "Invalid listen directive format: %s", value.c_str());
            return false;
        }
        host_str = DEFAULT_HOST;  // Default to all interfaces
    }

    // Store the original hostname
    virtual_server.host_name_ = host_str;

    // Special case for "0.0.0.0" (INADDR_ANY) - no need to resolve
    if (host_str == "0.0.0.0") {
        virtual_server.host_ = host_str;
    } else {
        // Check if already a valid IP address
        struct in_addr addr;
        if (inet_pton(AF_INET, host_str.c_str(), &addr) == 1) {
            // Already a valid IP, keep it
            virtual_server.host_ = host_str;
        } else {
            // Resolve hostname to IP
            struct addrinfo hints, *res;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;  // IPv4 only
            hints.ai_socktype = SOCK_STREAM;

            int status = getaddrinfo(host_str.c_str(), NULL, &hints, &res);
            if (status != 0) {
                log(LOG_ERROR, "Error resolving hostname '%s': %s",
                    host_str.c_str(), gai_strerror(status));
                return false;
            }

            // Convert the resolved address to string and store it
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &((struct sockaddr_in*)res->ai_addr)->sin_addr,
                      ip_str, sizeof(ip_str));
            virtual_server.host_ = ip_str;

            // Free the linked list returned by getaddrinfo
            freeaddrinfo(res);
        }
    }

    return true;
}

bool VirtualServer::parse_client_max_body_size(const std::string& value,
                                               VirtualServer& virtual_server) {
    if (value.empty()) {
        log(LOG_ERROR, "client_max_body_size cannot be empty");
        return false;
    }

    size_t size = 0;
    std::string numPart;
    char unit = '\0';

    // Check if last character is a unit
    if (isalpha(value[value.length() - 1])) {
        unit = value[value.length() - 1];
        numPart = value.substr(0, value.length() - 1);
    } else {
        numPart = value;
    }

    // Check that numPart contains only digits
    for (size_t i = 0; i < numPart.length(); i++) {
        if (!isdigit(numPart[i])) {
            log(LOG_ERROR, "Invalid client_max_body_size value: %s", value.c_str());
            return false;
        }
    }

    // Parse the numeric part
    std::istringstream iss(numPart);
    if (!(iss >> size)) {
        log(LOG_ERROR, "Invalid number format in client_max_body_size: %s",
            value.c_str());
        return false;
    }

    // Apply unit multiplier if present
    if (unit != '\0') {
        switch (toupper(unit)) {
            case 'K':
                size *= 1024;
                break;
            case 'M':
                size *= 1024 * 1024;
                break;
            case 'G':
                size *= 1024 * 1024 * 1024;
                break;
            default:
                log(LOG_ERROR, "Unknown size unit '%c' in client_max_body_size", unit);
                return false;
        }
    }

    // Check for zero
    if (size == 0) {
        log(LOG_ERROR, "client_max_body_size cannot be zero");
        return false;
    }

    virtual_server.client_max_body_size_ = size;
    return true;
}

bool VirtualServer::parse_server_name(const std::string& value,
                                      VirtualServer& virtual_server) {
    std::istringstream iss(value);
    std::string name;
    while (iss >> name) {
        virtual_server.server_names_.push_back(name);
    }
    return true;
}

bool VirtualServer::parse_error_page(const std::string& value,
                                     VirtualServer& virtual_server) {
    std::istringstream iss(value);
    int code;
    std::string path;
    if (iss >> code >> path) {
        virtual_server.error_pages_[code] = path;
        return true;
    }
    log(LOG_ERROR, "Error parsing error_page directive: %s", value.c_str());
    return false;
}

bool VirtualServer::add_directive_value(Location& location,
                                        const std::string& key,
                                        const std::string& value) {
    if (key == "root") {
        // Modification - Carol
        if (value[0] == '/') {
            location.root_  = value.substr(1);  // "/var/www/example.com" becomes "var/www/example.com"
        } 
        else {
            location.root_ = value;
        }
    } else if (key == "autoindex") {
        location.autoindex_ = (value == "on");
    } else if (key == "allow_methods") {
        // Clear default methods first
        location.allowed_methods_.clear();

        // Split and add methods
        std::istringstream iss(value);
        std::string method;
        while (iss >> method) {
            location.allowed_methods_.push_back(method);
        }
    } else if (key == "cgi") {
        location.cgi_enabled_ = (value == "on");
    } else if (key == "index") {
        std::istringstream iss(value);
        std::string first_index;
        if (iss >> first_index) {
            location.index_ = first_index;
            // Check if there are additional index files (for logging)
            std::string remaining;
            if (std::getline(iss, remaining) && !remaining.empty()) {
                log(LOG_INFO, "Multiple index files specified, using first: %s (ignoring: %s)", 
                    first_index.c_str(), remaining.c_str());
            }
        } else {
            location.index_ = DEFAULT_INDEX;
        }
    } else if (key == "redirect") {
        location.redirect_ = value;
    } else {
        log(LOG_ERROR, "Unknown directive in location block: %s", key.c_str());
        return false;
    }
    return true;
}

// Helper to parse a single directive line
bool VirtualServer::parse_directive(const std::string& line, std::string& key,
                                    std::string& value) {
    size_t pos = line.find_first_of(" \t");

    if (pos == std::string::npos) {
        return false;
    }

    key = line.substr(0, pos);

    // Check for comment character before further processing
    size_t commentPos = line.find('#');
    std::string effectiveLine = line;

    // If comment exists, only consider the part before it
    if (commentPos != std::string::npos) {
        effectiveLine = line.substr(0, commentPos);
    }

    // Find the start of the value (skip whitespace)
    size_t valueStart = effectiveLine.find_first_not_of(" \t", pos);
    if (valueStart == std::string::npos) return false;

    // Find the end of the value (either semicolon or end of line)
    size_t valueEnd = effectiveLine.find(';', valueStart);
    if (valueEnd == std::string::npos) {
        valueEnd = effectiveLine.length();
    }

    value = effectiveLine.substr(valueStart, valueEnd - valueStart);
    value = trim(value);

    return !value.empty();
}

bool VirtualServer::apply_defaults() {
    // Apply server-level defaults
    if (server_names_.empty()) {
        server_names_.push_back(DEFAULT_SERVER_NAME);
    }

    // Default error pages if not specified
    if (error_pages_.find(DEFAULT_404_ERROR_CODE) == error_pages_.end()) {
        error_pages_[DEFAULT_404_ERROR_CODE] = DEFAULT_404_ERROR_PAGE;
    }
    if (error_pages_.find(DEFAULT_500_ERROR_CODE) == error_pages_.end()) {
        error_pages_[DEFAULT_500_ERROR_CODE] = DEFAULT_500_ERROR_PAGE;
    }

    return true;
}

// Implementation of validation methods
bool VirtualServer::is_valid_host() const {
    // Check if the host is valid IP address format
    std::string::size_type start = 0;
    int octets = 0;

    while (start < host_.length() && octets < 4) {
        std::string::size_type end = host_.find('.', start);
        if (octets < 3 && end == std::string::npos) {
            log(LOG_ERROR, "Invalid IP address format: %s", host_.c_str());
            return false;
        }
        if (octets == 3) {
            end = host_.length();
        }

        std::string octet = host_.substr(start, end - start);
        // Check if octet is a valid number
        for (size_t i = 0; i < octet.length(); i++) {
            if (!isdigit(octet[i])) {
                log(LOG_ERROR, "Invalid IP address format (non-digit): %s", host_.c_str());
                return false;
            }
        }

        // Check octet range (0-255)
        int value = atoi(octet.c_str());
        if (value < 0 || value > 255) {
            log(LOG_ERROR, "Invalid IP address (octet out of range): %s", host_.c_str());
            return false;
        }

        start = end + 1;
        octets++;
    }

    if (octets != 4 || start != host_.length() + 1) {
        log(LOG_ERROR, "Invalid IP address (incorrect format): %s", host_.c_str());
        return false;
    }

    return true;
}

bool VirtualServer::is_valid_port() const {
    if (port_ <= 0 || port_ > 65535) {
        log(LOG_ERROR, "Invalid port number: %d", port_);
        return false;
    }
    return true;
}

bool VirtualServer::has_valid_locations() const {
    if (locations_.empty()) {
        log(LOG_ERROR, "Server must have at least one location block");
        return false;
    }

    // Validate each location
    for (size_t i = 0; i < locations_.size(); i++) {
        if (!locations_[i].is_valid()) {
            log(LOG_ERROR, "Invalid location block: %s", locations_[i].path_.c_str());
            return false;
        }
    }

    return true;
}

bool VirtualServer::has_valid_error_pages() const {
    for (std::map<int, std::string>::const_iterator it = error_pages_.begin();
         it != error_pages_.end(); ++it) {
        
        const std::string& error_page_path = it->second;
        
        // Check if the error page file exists and is readable
        struct stat file_stat;
        if (stat(error_page_path.c_str(), &file_stat) != 0) {
            // File doesn't exist - this is okay, we'll use default error pages
            log(LOG_WARNING, "Error page file does not exist: %s (will use default)", 
                error_page_path.c_str());
            continue;
        }
        
        if (!S_ISREG(file_stat.st_mode)) {
            log(LOG_ERROR, "Error page path is not a regular file: %s", error_page_path.c_str());
            return false;
        }
        
        if (access(error_page_path.c_str(), R_OK) != 0) {
            log(LOG_ERROR, "No read permission for error page: %s", error_page_path.c_str());
            return false;
        }
    }
    
    return true;
}

bool VirtualServer::is_valid() const {
    if (!listen_specified_) {
        log(LOG_ERROR, "Listen directive is mandatory");
        return false;
    }

    if (!is_valid_host()) {
        return false;
    }

    if (!is_valid_port()) {
        return false;
    }

    if (!has_valid_locations()) {
        return false;
    }

    if (!has_valid_error_pages()) {
        return false;
    }

    return true;
}

bool Location::is_valid() const {
    if (path_.empty()) {
        log(LOG_ERROR, "Location path is required");
        return false;
    }

    // Check if path starts with /
    if (path_[0] != '/') {
        log(LOG_ERROR, "Location path must start with /: %s", path_.c_str());
        return false;
    }

    // Check for invalid characters in path
    const std::string invalidChars = "<>\"'|*?";
    for (size_t i = 0; i < invalidChars.length(); i++) {
        if (path_.find(invalidChars[i]) != std::string::npos) {
            log(LOG_ERROR, "Location path contains invalid character '%c': %s", 
                invalidChars[i], path_.c_str());
            return false;
        }
    }

    // Check if redirect is valid when specified
    if (!redirect_.empty()) {
        std::istringstream iss(redirect_);
        std::string status_str, url;
        
        if (iss >> status_str >> url) {
            // Parse and validate status code
            int status_code = atoi(status_str.c_str());
            if (status_code < 300 || status_code > 399) {
                log(LOG_ERROR, "Invalid redirect status code: %s (must be 300-399)", status_str.c_str());
                return false;
            }
            
            // Validate URL format - must be absolute URL or relative path
            if (url.empty()) {
                log(LOG_ERROR, "Redirect URL cannot be empty");
                return false;
            }
            
            if (url[0] != '/' && 
                url.find("http://") != 0 && 
                url.find("https://") != 0) {
                log(LOG_ERROR, "Redirect URL must be absolute URL or path starting with /: %s", url.c_str());
                return false;
            }
            
            // Check for additional parameters and warn
            std::string extra;
            if (iss >> extra) {
                log(LOG_INFO, "Multiple redirect parameters specified, using first URL: %s", url.c_str());
            }
        } else {
            log(LOG_ERROR, "Invalid redirect format: expected 'status_code URL' but got: %s", redirect_.c_str());
            return false;
        }
    }

    if (root_.empty()) {
        log(LOG_ERROR, "Root directive is mandatory for location: %s", path_.c_str());
        return false;
    }

    // Validate root path exists and is a directory
    struct stat path_stat;
    log(LOG_DEBUG, "Checking root directory: %s", root_.c_str());
    if (stat(root_.c_str(), &path_stat) != 0) {
        log(LOG_ERROR, "Root directory does not exist: %s", root_.c_str());
        return false;
    }

    if (!S_ISDIR(path_stat.st_mode)) {
        log(LOG_ERROR, "Root path is not a directory: %s", root_.c_str());
        return false;
    }

    if (access(root_.c_str(), R_OK) != 0) {
        log(LOG_ERROR, "No read permission for root directory: %s", root_.c_str());
        return false;
    }

    if (!allowed_methods_.empty()) {
        for (size_t i = 0; i < allowed_methods_.size(); i++) {
            const std::string& method = allowed_methods_[i];
            if (method != "GET" && method != "POST" && method != "DELETE") {
                log(LOG_ERROR, "Invalid HTTP method: %s", method.c_str());
                return false;
            }
        }
    } else {
        log(LOG_ERROR, "At least one HTTP method must be allowed for location: %s", path_.c_str());
        return false;
    }

    return true;
}