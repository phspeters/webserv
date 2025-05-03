#include "webserv.hpp"

// Default constructor implementation
ServerConfig::ServerConfig()
    : port_(80), listen_specified_(false), client_max_body_size_(1024 * 1024) {
    host_ = "0.0.0.0";
}

// Server block parsing implementation
bool ServerConfig::parseServerBlock(std::ifstream& file, ServerConfig& config) {
    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        // Check for block end
        if (line == "}") {
            config.applyDefaults();
            return true;
        }

        // Check for location block
        if (line.find("location") == 0) {
            // Extract location path
            size_t pathStart =
                line.find_first_not_of(" \t", 8);  // Skip "location"
            if (pathStart == std::string::npos) continue;

            size_t pathEnd = line.find_first_of(" \t{", pathStart);
            if (pathEnd == std::string::npos) continue;

            std::string path = line.substr(pathStart, pathEnd - pathStart);
            // std::cout << "DEBUG: Extracted location path: '" << path << "'"
            //           << std::endl;

            // Find opening brace
            size_t bracePos = line.find('{', pathEnd);
            if (bracePos == std::string::npos) {
                // Get next line and look for brace
                if (!std::getline(file, line)) return false;

                line = trim(line);
                if (line != "{") return false;
            }

            // Create a new location config
            LocationConfig location;
            location.path = path;

            // Parse location block
            while (std::getline(file, line)) {
                line = trim(line);

                // Skip empty lines and comments
                if (line.empty() || line[0] == '#') continue;

                // Check for block end
                if (line == "}") break;

                // Parse directive
                std::string key, value;
                if (parseDirective(line, key, value)) {
                    addDirectiveValue(location.directives, key, value);
                }
            }

            config.locations.push_back(location);
            continue;
        }

        // Parse regular directive
        std::string key, value;
        if (parseDirective(line, key, value)) {
            // Handle special directives
            if (key == "listen") {
                config.listen_specified_ = true;
                // Parse listen directive (host:port or just port)
                size_t colonPos = value.find(':');
                if (colonPos != std::string::npos) {
                    config.host_ = value.substr(0, colonPos);
                    std::istringstream(value.substr(colonPos + 1)) >>
                        config.port_;
                } else {
                    // Just a port_ number
                    std::istringstream iss(value);
                    if (iss >> config.port_) {
                        config.host_ = "0.0.0.0";  // Default to all interfaces
                    }
                }
            } else if (key == "server_name") {
                // Parse server_name directive (space-separated list)
                std::istringstream iss(value);
                std::string name;
                while (iss >> name) {
                    config.server_names_.push_back(name);
                }
            } else if (key == "error_page") {
                // Parse error_page directive (format: error_page CODE PATH)
                std::istringstream iss(value);
                int code;
                std::string path;
                if (iss >> code >> path) {
                    config.error_pages[code] = path;
                }
            } else if (key == "client_max_body_size") {
                // Parse size with unit (e.g., 8M)
                size_t size = 0;
                std::string sizeStr = value;
                char unit = sizeStr[sizeStr.length() - 1];

                if (isalpha(unit)) {
                    std::istringstream(
                        sizeStr.substr(0, sizeStr.length() - 1)) >>
                        size;
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
                    }
                } else {
                    std::istringstream(sizeStr) >> size;
                }

                config.client_max_body_size_ = size;
            } else {
                // Store other directives
                // Initialize vector if key doesn't exist
                if (config.directives.find(key) == config.directives.end()) {
                    config.directives[key] = std::vector<std::string>();
                }
                // Add value to the vector
                config.directives[key].push_back(value);
            }
        }
    }

    // Reached end of file without closing brace
    return false;
}

void ServerConfig::addDirectiveValue(
    std::map<std::string, std::vector<std::string> >& directives,
    const std::string& key, const std::string& value) {
    // Initialize vector if key doesn't exist
    if (directives.find(key) == directives.end()) {
        directives[key] = std::vector<std::string>();
    }

    // Special handling for space-separated directives
    if (key == "allow_methods") {
        // Split by spaces
        std::istringstream iss(value);
        std::string part;
        while (iss >> part) {
            directives[key].push_back(part);
        }
    } else {
        // Keep value as is
        directives[key].push_back(value);
    }
}

// Helper to parse a single directive line
bool ServerConfig::parseDirective(const std::string& line, std::string& key,
                                  std::string& value) {
    size_t pos = line.find_first_of(" \t");
    if (pos == std::string::npos) return false;

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
        server_names_.push_back("default_server");
    }

    // Default error pages if not specified
    if (error_pages.find(404) == error_pages.end()) {
        error_pages[404] = "/error/404.html";
    }
    if (error_pages.find(500) == error_pages.end()) {
        error_pages[500] = "/error/500.html";
    }

    // Apply defaults only to locations from the config file
    for (size_t i = 0; i < locations.size(); i++) {
        locations[i].applyDefaults();
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

    if (client_max_body_size_ <= 0) {
        error_msg = "Client max body size must be positive";
        return false;
    }

    if (!hasValidLocations(error_msg)) {
        return false;
    }

    return true;
}

// Initialize static member for directive mapping
std::map<std::string, LocationDirective> LocationConfig::directive_map;

// Initialize the directive map
void LocationConfig::initDirectiveMap() {
    if (directive_map.empty()) {
        directive_map["root"] = ROOT;
        directive_map["autoindex"] = AUTOINDEX;
        directive_map["allow_methods"] = ALLOWED_METHODS;
        directive_map["cgi"] = CGI;
        directive_map["index"] = INDEX;
        directive_map["redirect"] = REDIRECT;
        // directive_map["cgi_ext"] = CGI_EXT;
    }
}

// Convert string to directive enum
LocationDirective LocationConfig::getDirectiveType(
    const std::string& directive) {
    std::map<std::string, LocationDirective>::iterator it =
        directive_map.find(directive);
    if (it != directive_map.end()) {
        return it->second;
    }
    return UNKNOWN;
}

// Helper function for single value validation
bool LocationConfig::hasSingleValue(
    const std::map<std::string, std::vector<std::string> >& directives,
    const std::string& key, std::string& error_msg) {
    std::map<std::string, std::vector<std::string> >::const_iterator it =
        directives.find(key);
    if (it != directives.end()) {
        if (it->second.size() != 1) {
            error_msg = key + " directive must have exactly one value";
            return false;
        }
    }
    return true;
}

bool LocationConfig::isValid(std::string& error_msg) const {
    // Ensure directive map is initialized
    initDirectiveMap();

    // Check if path is defined
    if (path.empty()) {
        error_msg = "Location path is required";
        return false;
    }

    // Check that all directive keys are allowed
    for (std::map<std::string, std::vector<std::string> >::const_iterator it =
             directives.begin();
         it != directives.end(); ++it) {
        if (getDirectiveType(it->first) == UNKNOWN) {
            error_msg =
                "Invalid directive in location block: '" + it->first + "'";
            return false;
        }
    }

    // Check for mandatory root directive
    std::map<std::string, std::vector<std::string> >::const_iterator root_it =
        directives.find("root");
    if (root_it == directives.end() || root_it->second.empty()) {
        error_msg = "Root directive is mandatory";
        return false;
    }

    // Validate directives that must have exactly one value
    std::string single_value_dirs[] = {"root", "index", "autoindex", "cgi",
                                       "redirect"};
    for (size_t i = 0;
         i < sizeof(single_value_dirs) / sizeof(single_value_dirs[0]); i++) {
        if (!hasSingleValue(directives, single_value_dirs[i], error_msg)) {
            return false;
        }
    }

    // Validate autoindex value if present
    std::map<std::string, std::vector<std::string> >::const_iterator
        autoindex_it = directives.find("autoindex");
    if (autoindex_it != directives.end() && !autoindex_it->second.empty()) {
        std::string value = autoindex_it->second[0];
        if (value != "on" && value != "off") {
            error_msg = "Autoindex must be 'on' or 'off'";
            return false;
        }
    }

    // Validate cgi value if present
    std::map<std::string, std::vector<std::string> >::const_iterator cgi_it =
        directives.find("cgi");
    if (cgi_it != directives.end() && !cgi_it->second.empty()) {
        std::string value = cgi_it->second[0];
        if (value != "on" && value != "off") {
            error_msg = "Cgi must be 'on' or 'off'";
            // std::cout << "DEBUG: CGI validation failed: " << error_msg
            //           << std::endl;
            return false;
        }

        // Check if cgi is enabled but no cgi_ext is specified
        // if (value == "on") {
        //     std::map<std::string, std::vector<std::string> >::const_iterator
        //         cgi_ext_it = directives.find("cgi_ext");
        //     if (cgi_ext_it == directives.end() || cgi_ext_it->second.empty()) {
        //         error_msg = "Cgi is enabled but no cgi_ext specified";
        //         return false;
        //     }

        //     // Validate cgi_ext values - must be .py, .php, or .sh
        //     for (size_t i = 0; i < cgi_ext_it->second.size(); i++) {
        //         const std::string& ext = cgi_ext_it->second[i];
        //         if (ext != ".py" && ext != ".php" && ext != ".sh") {
        //             error_msg = "Invalid cgi_ext value: '" + ext +
        //                         "'. Only .py, .php, and .sh are supported";
        //             return false;
        //         }
        //     }
        // }
    }

    // Validate allowed_methods if present
    std::map<std::string, std::vector<std::string> >::const_iterator
        methods_it = directives.find("allow_methods");
    if (methods_it != directives.end() && !methods_it->second.empty()) {
        for (size_t i = 0; i < methods_it->second.size(); i++) {
            std::string method = methods_it->second[i];
            if (method != "GET" && method != "POST" && method != "DELETE") {
                error_msg = "Invalid HTTP method: " + method;
                return false;
            }
        }
    }

    return true;
}

void LocationConfig::applyDefaults() {
    // Default allow_methods if not specified
    if (directives.find("allow_methods") == directives.end()) {
        std::vector<std::string> methods;
        methods.push_back("GET");
        methods.push_back("POST");
        methods.push_back("DELETE");
        directives["allow_methods"] = methods;
    }

    // Default index if not specified
    if (directives.find("index") == directives.end()) {
        directives["index"] = std::vector<std::string>(1, "index.html");
    }

    // Default autoindex if not specified
    if (directives.find("autoindex") == directives.end()) {
        directives["autoindex"] = std::vector<std::string>(1, "off");
    }

    // Default cgi if not specified
    if (directives.find("cgi") == directives.end()) {
        directives["cgi"] = std::vector<std::string>(1, "off");
    }
}

void ServerConfig::print() const {
    std::cout << "---------- SERVER CONFIG ----------" << std::endl;
    std::cout << "Host: " << host_ << std::endl;
    std::cout << "Port: " << port_ << std::endl;

    // Print server names
    std::cout << "Server Names: ";
    if (server_names_.empty()) {
        std::cout << "(default server)";
    } else {
        for (size_t i = 0; i < server_names_.size(); ++i) {
            std::cout << server_names_[i];
            if (i < server_names_.size() - 1) {
                std::cout << ", ";
            }
        }
    }
    std::cout << std::endl;

    // Print client max body size with unit
    std::cout << "Client Max Body Size: ";
    if (client_max_body_size_ >= 1024 * 1024 * 1024) {
        std::cout << (client_max_body_size_ / (1024 * 1024 * 1024)) << "G";
    } else if (client_max_body_size_ >= 1024 * 1024) {
        std::cout << (client_max_body_size_ / (1024 * 1024)) << "M";
    } else if (client_max_body_size_ >= 1024) {
        std::cout << (client_max_body_size_ / 1024) << "K";
    } else {
        std::cout << client_max_body_size_ << " bytes";
    }
    std::cout << std::endl;

    // Print error pages
    std::cout << "Error Pages:" << std::endl;
    if (error_pages.empty()) {
        std::cout << "  (none)" << std::endl;
    } else {
        for (std::map<int, std::string>::const_iterator it =
                 error_pages.begin();
             it != error_pages.end(); ++it) {
            std::cout << "  " << it->first << " -> " << it->second << std::endl;
        }
    }

    // Print other directives
    std::cout << "Other Directives:" << std::endl;
    if (directives.empty()) {
        std::cout << "  (none)" << std::endl;
    } else {
        for (std::map<std::string, std::vector<std::string> >::const_iterator
                 it = directives.begin();
             it != directives.end(); ++it) {
            std::cout << "  " << it->first << ": ";
            for (size_t i = 0; i < it->second.size(); ++i) {
                std::cout << it->second[i];
                if (i < it->second.size() - 1) {
                    std::cout << ", ";
                }
            }
            std::cout << std::endl;
        }
    }

    // Print location blocks
    std::cout << "Location Blocks (" << locations.size() << "):" << std::endl;
    for (size_t i = 0; i < locations.size(); ++i) {
        std::cout << "  ---------- LOCATION: " << locations[i].path
                  << " ----------" << std::endl;

        for (std::map<std::string, std::vector<std::string> >::const_iterator
                 it = locations[i].directives.begin();
             it != locations[i].directives.end(); ++it) {
            std::cout << "    " << it->first << ": ";
            for (size_t j = 0; j < it->second.size(); ++j) {
                std::cout << it->second[j];
                if (j < it->second.size() - 1) {
                    std::cout << ", ";
                }
            }
            std::cout << std::endl;
        }

        if (locations[i].directives.empty()) {
            std::cout << "    (no directives)" << std::endl;
        }
    }

    std::cout << "----------------------------------" << std::endl;
}