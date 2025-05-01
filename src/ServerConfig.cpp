#include "webserv.hpp"

// Init ServerConfig object with default values
ServerConfig::ServerConfig()
    : host_("0.0.0.0"), port_(80), client_max_body_size_(1024 * 1024) {}

// Parse a line into key and value
bool ServerConfig::parseDirective(const std::string& line, std::string& key,
                                  std::string& value) {
    std::string trimmed = trim(line);

    // Skip empty lines and comments
    if (trimmed.empty() || trimmed[0] == '#') return false;

    // Check for trailing semicolon
    bool hasSemicolon = (trimmed[trimmed.size() - 1] == ';');
    if (hasSemicolon) {
        // Remove the semicolon
        trimmed = trimmed.substr(0, trimmed.size() - 1);
    }

    // Find the first whitespace to separate key and value
    size_t pos = trimmed.find_first_of(" \t");
    if (pos != std::string::npos) {
        key = trim(trimmed.substr(0, pos));
        value = trim(trimmed.substr(pos));
    } else {
        key = trimmed;
        value = "";
    }

    return true;
}

// Parse a server block
bool ServerConfig::parseServerBlock(std::ifstream& file, ServerConfig& config) {
    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        // Check for block end
        if (line == "}") return true;

        // Check for location block
        if (line.find("location") == 0) {
            // Extract location path
            size_t pathStart =
                line.find_first_not_of(" \t", 8);  // Skip "location"
            if (pathStart == std::string::npos) continue;

            size_t pathEnd = line.find_first_of(" \t{", pathStart);
            if (pathEnd == std::string::npos) continue;

            std::string path = line.substr(pathStart, pathEnd - pathStart);

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
                    location.directives[key] = value;
                }
            }

            config.locations.push_back(location);
            continue;
        }

        // Parse regular directive
        std::string key, value;
        if (parseDirective(line, key, value)) {
            // Handle special directives
            // TO DO: verify liten directive
            if (key == "listen") {
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
                config.directives[key] = value;
            }
        }
    }

    // Reached end of file without closing brace
    return false;
}

// Function to print server configuration (for debugging)
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
        for (std::map<std::string, std::string>::const_iterator it =
                 directives.begin();
             it != directives.end(); ++it) {
            std::cout << "  " << it->first << ": " << it->second << std::endl;
        }
    }

    // Print location blocks
    std::cout << "Location Blocks (" << locations.size() << "):" << std::endl;
    for (size_t i = 0; i < locations.size(); ++i) {
        std::cout << "  ---------- LOCATION: " << locations[i].path
                  << " ----------" << std::endl;

        for (std::map<std::string, std::string>::const_iterator it =
                 locations[i].directives.begin();
             it != locations[i].directives.end(); ++it) {
            std::cout << "    " << it->first << ": " << it->second << std::endl;
        }

        if (locations[i].directives.empty()) {
            std::cout << "    (no directives)" << std::endl;
        }
    }

    std::cout << "----------------------------------" << std::endl;
}
