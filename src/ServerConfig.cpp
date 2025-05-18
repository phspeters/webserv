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

static std::vector<std::string> createDefaultAllowedMethods() {
    std::vector<std::string> methods;
    methods.push_back("GET");
    methods.push_back("POST");
    methods.push_back("DELETE");
    return methods;
}

static const std::vector<std::string> DEFAULT_ALLOWED_METHODS =
    createDefaultAllowedMethods();

// Constructor for LocationConfig with defaults
LocationConfig::LocationConfig()
    : autoindex(DEFAULT_AUTOINDEX),
      cgi_enabled(DEFAULT_CGI_ENABLED),
      index(DEFAULT_INDEX) {
    allowed_methods = DEFAULT_ALLOWED_METHODS;
}

// Default constructor implementation
ServerConfig::ServerConfig()
    : port_(DEFAULT_PORT),
      listen_specified_(false),
      client_max_body_size_(DEFAULT_MAX_BODY_SIZE) {
    host_ = DEFAULT_HOST;
}

bool ServerConfig::parseServerBlock(std::ifstream& file, ServerConfig& config) {
    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);

        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Check for block end
        if (line == "}") {
            config.applyDefaults();
            return true;
        }

        if (line.find("location") == 0) {
            if (!parseLocationBlock(file, line, config)) {
                return false;
            }
            continue;
        }

        std::string key, value;
        if (parseDirective(line, key, value)) {
            if (!handleServerDirective(key, value, config)) {
                return false;
            }
        } else {
            std::cerr << "Error: Invalid directive in server block: " << line
                      << std::endl;
            return false;
        }
    }

    // Reached end of file without closing brace
    return false;
}

bool ServerConfig::parseLocationBlock(std::ifstream& file, std::string line,
                                      ServerConfig& config) {
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

    // Create a new location config
    LocationConfig location;
    location.path = path;

    // Parse location block
    std::string locLine;
    while (std::getline(file, locLine)) {
        locLine = trim(locLine);

        if (locLine.empty() || locLine[0] == '#') {
            continue;
        }

        if (locLine == "}") {
            config.locations.push_back(location);
            return true;
        }

        std::string key, value;
        if (parseDirective(locLine, key, value)) {
            if (!addDirectiveValue(location, key, value)) {
                return false;  // Reject if directive isn't recognized
            }
        }
    }

    return false;
}

// Handle server directives with a separate method
bool ServerConfig::handleServerDirective(const std::string& key,
                                         const std::string& value,
                                         ServerConfig& config) {
    if (key == "listen") {
        config.listen_specified_ = true;
        return parseListen(value, config);
    } else if (key == "server_name") {
        return parseServerName(value, config);
    } else if (key == "error_page") {
        return parseErrorPage(value, config);
    } else if (key == "client_max_body_size") {
        return parseClientMaxBodySize(value, config);
    } else {
        std::cerr << "Error: Unknown directive '" << key << "'" << std::endl;
        return false;
    }
}

bool ServerConfig::parseListen(const std::string& value, ServerConfig& config) {
    size_t colonPos = value.find(':');
    if (colonPos != std::string::npos) {
        config.host_ = value.substr(0, colonPos);
        std::istringstream iss(value.substr(colonPos + 1));
        if (!(iss >> config.port_)) {
            std::cerr << "Error parsing port in listen directive: " << value
                      << std::endl;
            return false;
        }
    } else {
        // Just a port number
        std::istringstream iss(value);
        if (!(iss >> config.port_)) {
            std::cerr << "Error parsing port in listen directive: " << value
                      << std::endl;
            return false;
        }
        config.host_ = DEFAULT_HOST;  // Default to all interfaces
    }
    return true;
}

bool ServerConfig::parseClientMaxBodySize(const std::string& value,
                                          ServerConfig& config) {
    if (value.empty()) {
        std::cerr << "Error: client_max_body_size cannot be empty" << std::endl;
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
            std::cerr << "Error: client_max_body_size must contain only digits"
                      << std::endl;
            return false;
        }
    }

    // Parse the numeric part
    std::istringstream iss(numPart);
    if (!(iss >> size)) {
        std::cerr << "Error: invalid number format in client_max_body_size"
                  << std::endl;
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
                std::cerr << "Error: unknown size unit '" << unit << "'"
                          << std::endl;
                return false;
        }
    }

    // Check for zero
    if (size == 0) {
        std::cerr << "Error: client_max_body_size cannot be zero" << std::endl;
        return false;
    }

    config.client_max_body_size_ = size;
    return true;
}

bool ServerConfig::parseServerName(const std::string& value,
                                   ServerConfig& config) {
    std::istringstream iss(value);
    std::string name;
    while (iss >> name) {
        config.server_names_.push_back(name);
    }
    return true;
}

bool ServerConfig::parseErrorPage(const std::string& value,
                                  ServerConfig& config) {
    std::istringstream iss(value);
    int code;
    std::string path;
    if (iss >> code >> path) {
        config.error_pages[code] = path;
        return true;
    }
    std::cerr << "Error parsing error_page directive: " << value << std::endl;
    return false;
}

bool ServerConfig::addDirectiveValue(LocationConfig& location,
                                     const std::string& key,
                                     const std::string& value) {
    if (key == "root") {
        location.root = value;
    } else if (key == "autoindex") {
        location.autoindex = (value == "on");
    } else if (key == "allow_methods") {
        // Clear default methods first
        location.allowed_methods.clear();

        // Split and add methods
        std::istringstream iss(value);
        std::string method;
        while (iss >> method) {
            location.allowed_methods.push_back(method);
        }
    } else if (key == "cgi") {
        location.cgi_enabled = (value == "on");
    } else if (key == "index") {
        location.index = value;
    } else if (key == "redirect") {
        location.redirect = value;
    } else {
        std::cerr << "Unknown directive in location block: " << key
                  << std::endl;
        return false;
    }
    return true;
}

// Helper to parse a single directive line
bool ServerConfig::parseDirective(const std::string& line, std::string& key,
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

    // std::cout << "DEBUG: Parsed directive: '" << key << "' with value: '"
    //           << value << "'" << std::endl;

    return !value.empty();
}

bool ServerConfig::applyDefaults() {
    // Apply server-level defaults
    if (server_names_.empty()) {
        server_names_.push_back(DEFAULT_SERVER_NAME);
    }

    // Default error pages if not specified
    if (error_pages.find(DEFAULT_404_ERROR_CODE) == error_pages.end()) {
        error_pages[DEFAULT_404_ERROR_CODE] = DEFAULT_404_ERROR_PAGE;
    }
    if (error_pages.find(DEFAULT_500_ERROR_CODE) == error_pages.end()) {
        error_pages[DEFAULT_500_ERROR_CODE] = DEFAULT_500_ERROR_PAGE;
    }

    return true;
}

// Implementation of validation methods
bool ServerConfig::isValidHost(std::string& error_msg) const {
    // Check if the host is valid IP address format
    std::string::size_type start = 0;
    int octets = 0;

    while (start < host_.length() && octets < 4) {
        std::string::size_type end = host_.find('.', start);
        if (octets < 3 && end == std::string::npos) {
            error_msg = "Invalid IP address format: " + host_;
            return false;
        }
        if (octets == 3) {
            end = host_.length();
        }

        std::string octet = host_.substr(start, end - start);
        // Check if octet is a valid number
        for (size_t i = 0; i < octet.length(); i++) {
            if (!isdigit(octet[i])) {
                error_msg = "Invalid IP address format (non-digit): " + host_;
                return false;
            }
        }

        // Check octet range (0-255)
        int value = atoi(octet.c_str());
        if (value < 0 || value > 255) {
            error_msg = "Invalid IP address (octet out of range): " + host_;
            return false;
        }

        start = end + 1;
        octets++;
    }

    if (octets != 4 || start != host_.length() + 1) {
        error_msg = "Invalid IP address (incorrect format): " + host_;
        return false;
    }

    return true;
}

bool ServerConfig::isValidPort(std::string& error_msg) const {
    if (port_ <= 0 || port_ > 65535) {
        error_msg =
            "Invalid port number: " +
            static_cast<std::ostringstream*>(&(std::ostringstream() << port_))
                ->str();
        return false;
    }
    return true;
}

bool ServerConfig::hasValidLocations(std::string& error_msg) const {
    if (locations.empty()) {
        error_msg = "Server must have at least one location block";
        return false;
    }

    // Validate each location
    for (size_t i = 0; i < locations.size(); i++) {
        std::string location_error;
        if (!locations[i].isValid(location_error)) {
            error_msg = "Invalid location [" + locations[i].path +
                        "]: " + location_error;
            // std::cout << "DEBUG: Location validation failed: " << error_msg
            //           << std::endl;
            return false;
        }
    }

    // std::cout << "DEBUG: All locations validated successfully" << std::endl;

    return true;
}

bool ServerConfig::isValid(std::string& error_msg) const {
    if (!listen_specified_) {
        error_msg = "Listen directive is mandatory";
        return false;
    }

    if (!isValidHost(error_msg)) {
        return false;
    }

    if (!isValidPort(error_msg)) {
        return false;
    }

    if (!hasValidLocations(error_msg)) {
        return false;
    }

    return true;
}

bool LocationConfig::isValid(std::string& error_msg) const {
    if (path.empty()) {
        error_msg = "Location path is required";
        return false;
    }

    // Check if path starts with /
    if (path[0] != '/') {
        error_msg = "Location path must start with /: " + path;
        return false;
    }
    
    // Check for invalid characters in path
    const std::string invalidChars = "<>\"'|*?";
    for (size_t i = 0; i < invalidChars.length(); i++) {
        if (path.find(invalidChars[i]) != std::string::npos) {
            error_msg = "Location path contains invalid character '" + 
                        std::string(1, invalidChars[i]) + "': " + path;
            return false;
        }
    }
    
    // Check if redirect is valid when specified
    if (!redirect.empty() && redirect[0] != '/' && 
        redirect.find("http://") != 0 && redirect.find("https://") != 0) {
        error_msg = "Redirect must be an absolute path or URL: " + redirect;
        return false;
    }

    if (root.empty()) {
        error_msg = "Root directive is mandatory";
        return false;
    }

    // Validate root path exists and is a directory
    // struct stat path_stat;
    // if (stat(root.c_str(), &path_stat) != 0) {
    //     error_msg = "Root directory does not exist: " + root;
    //     return false;
    // }
    
    // if (!S_ISDIR(path_stat.st_mode)) {
    //     error_msg = "Root path is not a directory: " + root;
    //     return false;
    // }
    
    // Check read permissions
    // if (access(root.c_str(), R_OK) != 0) {
    //     error_msg = "No read permission for root directory: " + root;
    //     return false;
    // }

    if (!allowed_methods.empty()) {
        for (size_t i = 0; i < allowed_methods.size(); i++) {
            const std::string& method = allowed_methods[i];
            if (method != "GET" && method != "POST" && method != "DELETE") {
                error_msg = "Invalid HTTP method: " + method;
                return false;
            }
        }
    } else {
        error_msg = "At least one HTTP method must be allowed";
        return false;
    }

    return true;
}

const LocationConfig* ServerConfig::findMatchingLocation(const std::string& uri) const {
    const LocationConfig* best_match = NULL;
    
    for (std::vector<LocationConfig>::const_iterator it = locations.begin(); it != locations.end(); ++it) {
        const LocationConfig& location = *it;
        // Check if the request path starts with the location path
        if (uri.find(location.path) == 0) {
            // Make sure we match complete segments
            if (location.path == "/" ||           // Root always matches
                uri == location.path ||          // Exact match
                (uri.length() > location.path.length() && 
                (uri[location.path.length()] == '/' || location.path[location.path.length() - 1] == '/'))) { 
                if (!best_match || location.path.length() > best_match->path.length()) { 
                    best_match = &location;
                }
            }
        }
    }
    
    return best_match;
}
