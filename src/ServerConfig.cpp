#include "ServerConfig.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring> 
#include <cstddef> 
#include <stdexcept> 

// Constructor - sets global defaults and a fallback server block
ServerConfig::ServerConfig()
    : port(80), 
      server_root("./www"), 
      default_index("index.html"), 
      max_request_body_size(1024 * 1024), 
      connection_timeout_seconds(60) { 

    setDefaultMimeTypes();
    setDefaultCgiHandlers();
    setDefaultServerBlock();
}

void ServerConfig::setDefaultServerBlock() {
    // Create a minimal default server block configuration
    // This will be used if loadFromFile fails or finds no servers.
    // It will be cleared if loadFromFile succeeds.
    ServerBlock defaultServer;

    // Add a default root location to the default server
    RouteConfig defaultRoute;
    defaultRoute.path = "/";
    defaultRoute.root = this->server_root;
    defaultRoute.index_file = this->default_index; 
    defaultRoute.allowed_methods.push_back("GET"); 
    defaultRoute.allowed_methods.push_back("HEAD");

    defaultServer.routes.push_back(defaultRoute);
    servers.push_back(defaultServer); // Add this default server
}

// Set up default MIME types
void ServerConfig::setDefaultMimeTypes() {
    mime_types[".html"] = "text/html";
    mime_types[".htm"] = "text/html";
    mime_types[".css"] = "text/css";
    mime_types[".js"] = "application/javascript";
    mime_types[".json"] = "application/json";
    mime_types[".xml"] = "application/xml";
    mime_types[".txt"] = "text/plain";
    mime_types[".jpg"] = "image/jpeg";
    mime_types[".jpeg"] = "image/jpeg";
    mime_types[".png"] = "image/png";
    mime_types[".gif"] = "image/gif";
    mime_types[".svg"] = "image/svg+xml";
    mime_types[".ico"] = "image/x-icon";
    mime_types[".pdf"] = "application/pdf";
    mime_types[".zip"] = "application/zip";
    mime_types[".gz"] = "application/gzip";
    mime_types[".tar"] = "application/x-tar";
    // Add more as needed
}

// Set up default CGI handlers (paths might need adjustment based on system)
void ServerConfig::setDefaultCgiHandlers() {
    cgi_executables[".php"] = "/usr/bin/php-cgi"; // Example path
    cgi_executables[".py"] = "/usr/bin/python3";  // Example path
    cgi_executables[".pl"] = "/usr/bin/perl";    // Example path
    // Add more as needed
}

// Helper to get file extension including the dot
std::string ServerConfig::getFileExtension(const std::string& filename) const {
    size_t pos = filename.find_last_of('.');
    if (pos != std::string::npos) {
        return filename.substr(pos);
    }
    return ""; // No extension found
}

// Get MIME type for file based on extension
std::string ServerConfig::getMimeType(const std::string& filename) const {
    std::string ext = getFileExtension(filename);
    if (!ext.empty()) {
        std::map<std::string, std::string>::const_iterator it = mime_types.find(ext);
        if (it != mime_types.end()) {
            return it->second;
        }
    }
    return "application/octet-stream"; // Default MIME type
}

// Check if a file extension is associated with a CGI handler
bool ServerConfig::isCgiExtension(const std::string& filename) const {
    std::string ext = getFileExtension(filename);
    return !ext.empty() && cgi_executables.count(ext);
}

// Get CGI handler for a file extension
std::string ServerConfig::getCgiHandler(const std::string& filename) const {
    std::string ext = getFileExtension(filename);
    if (!ext.empty()) {
        std::map<std::string, std::string>::const_iterator it = cgi_executables.find(ext);
        if (it != cgi_executables.end()) {
            return it->second;
        }
    }
    return ""; // No CGI handler found
}

// Helper to skip empty lines and lines starting with '#'
bool ServerConfig::skipEmptyAndCommentLines(std::istream& stream, std::string& line) {
    while (std::getline(stream, line)) {
        size_t first_char_pos = line.find_first_not_of(" \t");
        if (first_char_pos != std::string::npos && line[first_char_pos] != '#') {
            return true; // Found a non-empty, non-comment line
        }
        // Continue if line is empty or a comment
    }
    return false; // Reached end of stream
}

// Trim whitespace from the beginning and end of a string
std::string ServerConfig::trimString(const std::string& str) {
    if (str.empty()) return str;
    
    size_t first = str.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, last - first + 1);
}

// Handle CGI extension directives (cgi_ext.php style)
void ServerConfig::handleCgiExtDirective(std::map<std::string, std::string>& directives, 
                                         const std::string& line) {
    size_t dot_pos = line.find("cgi_ext.");
    if (dot_pos == std::string::npos) return;
    
    std::string key = "cgi_extension";
    std::string ext = line.substr(dot_pos + 7); // Skip "cgi_ext."
    
    // Remove semicolon and whitespace
    size_t semicolon_pos = ext.find(';');
    if (semicolon_pos != std::string::npos) {
        ext = ext.substr(0, semicolon_pos);
    }
    
    // Trim any remaining whitespace
    ext = trimString(ext);
    
    // Make sure extension starts with a dot
    if (!ext.empty() && ext[0] != '.') {
        ext = "." + ext;
    }
    
    directives[key] = ext;
}

// Handle key-only directives (with or without semicolon)
void ServerConfig::handleKeyOnlyDirective(std::map<std::string, std::string>& directives, 
                                          const std::string& trimmed_line, 
                                          size_t first_char) {
    std::string key = trimmed_line.substr(first_char);
    size_t semi_colon = key.find(';');
    if (semi_colon != std::string::npos) {
        key = key.substr(0, semi_colon);
    }
    
    // Trim any trailing whitespace
    key = trimString(key);
    
    if (!key.empty()) directives[key] = ""; // Store key with empty value
}

// Handle key-value directives
void ServerConfig::handleKeyValueDirective(std::map<std::string, std::string>& directives, 
                                           const std::string& trimmed_line, 
                                           size_t first_char, 
                                           size_t first_space) {
    std::string key = trimmed_line.substr(first_char, first_space - first_char);
    
    size_t value_start = trimmed_line.find_first_not_of(" \t", first_space);
    if (value_start == std::string::npos) {
        // Key without a value, potentially ending in ;
        size_t semi_colon = key.find(';');
        if (semi_colon != std::string::npos) {
            key = key.substr(0, semi_colon);
        }
        
        key = trimString(key);
        
        if (!key.empty()) directives[key] = "";
        return;
    }
    
    // Extract value, trim trailing whitespace and optional semicolon
    std::string value = trimmed_line.substr(value_start);
    size_t semi_colon = value.find(';');
    if (semi_colon != std::string::npos) {
        value = value.substr(0, semi_colon);
    }
    
    value = trimString(value);
    
    directives[key] = value;
}

// Parse a single line into key-value pairs (refactored)
void ServerConfig::parseLine(const std::string& line,
                            std::map<std::string, std::string>& directives) {
    std::string trimmed_line = line;
    size_t comment_pos = trimmed_line.find('#');
    if (comment_pos != std::string::npos) {
        trimmed_line = trimmed_line.substr(0, comment_pos);
    }

    size_t first_char = trimmed_line.find_first_not_of(" \t");
    if (first_char == std::string::npos) return; // Line is empty or whitespace only

    // Special case for CGI directives with dot syntax (cgi_ext.php)
    if (trimmed_line.find("cgi_ext.") != std::string::npos) {
        handleCgiExtDirective(directives, trimmed_line);
        return;
    }

    size_t first_space = trimmed_line.find_first_of(" \t", first_char);
    if (first_space == std::string::npos) {
        // Maybe a directive with no value
        handleKeyOnlyDirective(directives, trimmed_line, first_char);
        return;
    }

    // Handle standard key-value directive
    handleKeyValueDirective(directives, trimmed_line, first_char, first_space);
}

// Parse listen directive for server block
bool ServerConfig::parseListenDirective(ServerBlock& server, const std::string& value) {
    size_t colon_pos = value.find(':');
    if (colon_pos != std::string::npos) {
        server.host = value.substr(0, colon_pos);
        std::istringstream ss(value.substr(colon_pos + 1));
        if (!(ss >> server.port)) {
            std::cerr << "Error: Invalid port number in listen directive: " << value << std::endl;
            return false;
        }
    } else {
        // Check if value is just a port or just a host/address
        bool is_numeric_port = true;
        for (size_t i = 0; i < value.length(); ++i) {
            if (!isdigit(value[i])) { 
                is_numeric_port = false; 
                break; 
            }
        }
        if (is_numeric_port) {
            std::istringstream ss(value);
            if (!(ss >> server.port)) {
                std::cerr << "Error: Invalid port number in listen directive: " << value << std::endl;
                return false;
            }
            // Keep default host 
        } else {
            server.host = value;
            // Keep default port 
        }
    }
    return true;
}

// Parse server_name directive
void ServerConfig::parseServerNameDirective(ServerBlock& server, const std::string& value) {
    std::istringstream ss(value);
    std::string name;
    while (ss >> name) {
        if (!name.empty()) { 
            server.server_names.push_back(name); 
        }
    }
}

// Parse client_max_body_size directive
bool ServerConfig::parseMaxBodySizeDirective(ServerBlock& server, const std::string& value) {
    size_t size_val = 0;
    std::string size_str = value;
    char unit = ' ';
    // Check last char for unit, case-insensitive
    if (!size_str.empty()) {
        char last = std::tolower(size_str[size_str.length() - 1]);
        if (last == 'k' || last == 'm' || last == 'g') {
            unit = last;
            size_str = size_str.substr(0, size_str.length() - 1);
        }
    }
    std::istringstream ss(size_str);
    if (!(ss >> size_val)) {
        std::cerr << "Error: Invalid value for client_max_body_size: " << value << std::endl;
        return false;
    }

    if (unit == 'k') size_val *= 1024;
    else if (unit == 'm') size_val *= (1024 * 1024);
    else if (unit == 'g') size_val *= (1024 * 1024 * 1024);
    server.max_body_size = size_val;
    return true;
}

// Parse error_page directive
bool ServerConfig::parseErrorPageDirective(ServerBlock& server, const std::string& value) {
    std::istringstream ss(value);
    int code;
    std::string page_path;
    if (!(ss >> code)) {
        std::cerr << "Error: Invalid status code in error_page directive: " << value << std::endl;
        return false;
    }
    // The rest of the line is the path
    size_t path_start = value.find_first_not_of(" \t", ss.tellg() == std::streampos(-1) ? 0 : (size_t)ss.tellg());

    if (path_start != std::string::npos) {
        page_path = value.substr(path_start);
        server.error_pages[code] = page_path;
        return true;
    } else {
        std::cerr << "Error: Missing path in error_page directive: " << value << std::endl;
        return false;
    }
}

// Process a server-level directive
bool ServerConfig::processServerDirective(ServerBlock& server, const std::string& key, const std::string& value) {
    if (key == "listen") {
        return parseListenDirective(server, value);
    } else if (key == "server_name") {
        parseServerNameDirective(server, value);
        return true;
    } else if (key == "client_max_body_size") {
        return parseMaxBodySizeDirective(server, value);
    } else if (key == "error_page") {
        return parseErrorPageDirective(server, value);
    } else {
        std::cerr << "Warning: Unknown server directive '" << key << "' found. Ignoring." << std::endl;
        return true; // Not fatal
    }
}

// Check if a location block has a valid path
bool ServerConfig::validateLocationPath(const std::string& line, std::string& path) {
    size_t loc_pos = line.find("location");
    size_t path_start = line.find_first_not_of(" \t", loc_pos + 8); // Skip "location "
    size_t path_end = std::string::npos;

    if (path_start != std::string::npos) {
        path_end = line.find_first_of(" \t{", path_start);
        if (path_end != std::string::npos) {
            path = line.substr(path_start, path_end - path_start);
        } else {
            path = line.substr(path_start); // Path until end of line if no space/brace
        }
    }

    if (path.empty()) {
        std::cerr << "Error: Invalid location directive - missing path: " << line << std::endl;
        return false;
    }
    return true;
}

// Find opening brace for location block
bool ServerConfig::findLocationOpeningBrace(std::istream& config_file, const std::string& line, size_t& brace_pos) {
    std::string current_line = line;
    brace_pos = current_line.find('{');
    
    if (brace_pos == std::string::npos) {
        if (!skipEmptyAndCommentLines(config_file, current_line)) {
            std::cerr << "Error: Expected '{' after location path, but reached end of file." << std::endl;
            return false;
        }
        brace_pos = current_line.find('{');
    }

    if (brace_pos == std::string::npos) {
        std::cerr << "Error: Expected '{' to start location block, found: " << current_line << std::endl;
        return false;
    }
    
    return true;
}

// Parse root directive in location block
void ServerConfig::parseRootDirective(RouteConfig& route, const std::string& value) {
    route.root = value;
}

// Parse index directive in location block
void ServerConfig::parseIndexDirective(RouteConfig& route, const std::string& value) {
    std::istringstream ss(value);
    std::string index;
    ss >> index; // Just take the first one for now
    if (!index.empty()) {
        route.index_file = index;
    }
}

// Parse autoindex directive in location block
void ServerConfig::parseAutoindexDirective(RouteConfig& route, const std::string& value) {
    route.directory_listing = (value == "on");
}

// Parse return (redirect) directive in location block
bool ServerConfig::parseReturnDirective(RouteConfig& route, const std::string& value) {
    std::istringstream ss(value);
    int code = 0;
    std::string url;
    if (ss >> code) {
        // The rest is the URL
        size_t url_start = value.find_first_not_of(" \t", ss.tellg() == std::streampos(-1) ? 0 : (size_t)ss.tellg());
        if (url_start != std::string::npos) {
            url = value.substr(url_start);
        } else {
            std::cerr << "Error: Missing URL in return directive for location '" << route.path << "'" << std::endl;
            return false;
        }
        route.redirect_code = code;
        route.redirect_url = url;
        return true;
    } else {
        std::cerr << "Error: Invalid code in return directive for location '" << route.path << "'" << std::endl;
        return false;
    }
}

// Parse allow_methods or limit_except directive in location block
void ServerConfig::parseMethodsDirective(RouteConfig& route, const std::string& value) {
    route.allowed_methods.clear(); // Override any defaults if specified
    std::istringstream ss(value);
    std::string method;
    while (ss >> method) {
        if (!method.empty()) { 
            route.allowed_methods.push_back(method); 
        }
    }
    // Optional: Validate methods (GET, POST, PUT, DELETE, HEAD, etc.)
}

// Parse CGI directives in location block
void ServerConfig::parseCgiDirective(RouteConfig& route, const std::string& key, const std::string& value) {
    if (key == "cgi_pass" || key == "fastcgi_pass") {
        route.cgi_handler = value;
    } else if (key == "cgi_extension") {
        route.cgi_extension = value;
    }
}

// Parse upload directives in location block
void ServerConfig::parseUploadDirective(RouteConfig& route, const std::string& key, const std::string& value) {
    if (key == "upload_enable" || key == "accept_uploads") {
        route.uploads_enabled = (value == "on" || value == "true" || value == "yes" || value == "1");
    } else if (key == "upload_store" || key == "upload_dir" || key == "upload_path") {
        route.upload_dir = value;
    }
}

// Process a location-level directive
bool ServerConfig::processLocationDirective(RouteConfig& route, const std::string& key, const std::string& value) {
    if (key == "root") {
        parseRootDirective(route, value);
    } else if (key == "index") {
        parseIndexDirective(route, value);
    } else if (key == "autoindex") {
        parseAutoindexDirective(route, value);
    } else if (key == "return") {
        return parseReturnDirective(route, value);
    } else if (key == "allow_methods" || key == "limit_except") {
        parseMethodsDirective(route, value);
    } else if (key == "cgi_pass" || key == "fastcgi_pass" || key == "cgi_extension") {
        parseCgiDirective(route, key, value);
    } else if (key == "upload_enable" || key == "accept_uploads" || 
               key == "upload_store" || key == "upload_dir" || key == "upload_path") {
        parseUploadDirective(route, key, value);
    } else {
        std::cerr << "Warning: Unknown location directive '" << key << "' in location '" 
                 << route.path << "'. Ignoring." << std::endl;
    }
    return true;
}

// Parse a location block (expects stream position after '{')
bool ServerConfig::parseLocationBlock(std::istream& config_file, RouteConfig& route) {
    std::string line;
    std::map<std::string, std::string> directives;

    while (skipEmptyAndCommentLines(config_file, line)) {
        if (line.find('}') != std::string::npos) {
            // End of location block
            // Validation: check if essential fields like root are set?
            if (route.root.empty() && route.redirect_url.empty()) {
                std::cerr << "Warning: Location block '" << route.path 
                          << "' defined without a 'root' or 'redirect' directive. "
                          << "Behavior might be undefined." << std::endl;
                // Depending on strictness, could return false here.
            }
            return true;
        }

        // --- Location Directive ---
        directives.clear();
        parseLine(line, directives);

        if (!directives.empty()) {
            std::map<std::string, std::string>::const_iterator it = directives.begin();
            std::string key = it->first;
            std::string value = it->second;

            // Process the directive
            if (!processLocationDirective(route, key, value)) {
                return false; // Stop on error
            }
        }
    }

    std::cerr << "Error: Reached end of file while parsing location block '" 
              << route.path << "'. Missing '}'." << std::endl;
    return false;
}

// Parse a server block (expects the stream position after '{')
bool ServerConfig::parseServerBlock(std::istream& config_file) {
    ServerBlock current_server; // Uses ServerBlock constructor defaults initially
    std::string line;
    std::map<std::string, std::string> directives;

    while (skipEmptyAndCommentLines(config_file, line)) {
        if (line.find('}') != std::string::npos) {
            // End of server block
            servers.push_back(current_server); // Add the parsed server
            return true;
        }

        // Check for location block
        size_t loc_pos = line.find("location");
        size_t first_char = line.find_first_not_of(" \t");

        if (loc_pos != std::string::npos && loc_pos == first_char) {
            // --- Location Block ---
            std::string path;
            if (!validateLocationPath(line, path)) {
                return false;
            }

            // Find opening brace '{'
            size_t brace_pos;
            if (!findLocationOpeningBrace(config_file, line, brace_pos)) {
                return false;
            }
            
            RouteConfig current_route;
            current_route.path = path;

            if (!parseLocationBlock(config_file, current_route)) {
                return false; // Propagate error from location parsing
            }
            current_server.routes.push_back(current_route); // Add parsed route

        } else {
            // --- Server Directive ---
            directives.clear();
            parseLine(line, directives);

            if (!directives.empty()) {
                std::map<std::string, std::string>::const_iterator it = directives.begin();
                std::string key = it->first;
                std::string value = it->second;

                // Process the server directive
                if (!processServerDirective(current_server, key, value)) {
                    return false; // Stop on error
                }
            } 
        } 
    } 

    // If loop finishes because EOF was reached before finding '}'
    std::cerr << "Error: Reached end of file while parsing server block. Missing '}'." << std::endl;
    return false;
}

// Load configuration from file
bool ServerConfig::loadFromFile(const std::string& filename) {
    std::ifstream config_file(filename.c_str());
    if (!config_file.is_open()) {
        std::cerr << "Error: Failed to open config file: " << filename << std::endl;
        return false;
    }

    servers.clear(); // Clear any existing server blocks

    std::string line;
    try {
        while (skipEmptyAndCommentLines(config_file, line)) {
             // Expect 'server {' directive
             size_t server_pos = line.find("server");
             size_t brace_pos = line.find('{');

             // Allow "server" and "{" on same or separate lines
             if (server_pos != std::string::npos && server_pos == line.find_first_not_of(" \t")) {
                 if (brace_pos == std::string::npos) {
                     if (!skipEmptyAndCommentLines(config_file, line)) {
                          std::cerr << "Error: Expected '{' after 'server' directive, but reached end of file." << std::endl;
                          servers.clear(); return false; // Parsing error
                     }
                     brace_pos = line.find('{');
                 }

                 if (brace_pos != std::string::npos) {
                    if (!parseServerBlock(config_file)) {
                         servers.clear(); return false; // Propagate parsing error
                    }
                 } else {
                    std::cerr << "Error: Expected '{' after 'server' directive, found: " << line << std::endl;
                    servers.clear(); return false; // Parsing error
                 }
             } else {
                 std::cerr << "Error: Expected 'server' directive at top level, found: " << line << std::endl;
                 servers.clear(); return false; // Parsing error
             }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing config file: " << e.what() << std::endl;
        servers.clear(); // Clear partially parsed data on error
        return false;
    }

    return handleEmptyServerConfig(filename);
}

// Handle empty server configuration
bool ServerConfig::handleEmptyServerConfig(const std::string& filename) {
    // If parsing succeeded but no server blocks were defined
    if (servers.empty()) {
        std::cerr << "Warning: No server blocks defined in config file: " << filename 
                  << ". Using default server." << std::endl;
        // Re-add the default server configuration if parsing finished but found nothing
        ServerConfig default_config; // Create a temporary default config
        servers = default_config.servers; // Copy its default server setup
        // We *don't* return false here, as the file was read, just empty/invalid structure.
    }

    // Update ServerConfig members from the *first* parsed server block
    if (!servers.empty()) {
        updateConfigFromFirstServer();
    }

    return true;
}

// Update config from the first server
void ServerConfig::updateConfigFromFirstServer() {
    port = servers[0].port;
    max_request_body_size = servers[0].max_body_size;

    // Try to get root/index from the first server's root location if possible
    bool root_set = false;
    for(size_t i=0; i < servers[0].routes.size(); ++i) {
        if (servers[0].routes[i].path == "/") {
             server_root = servers[0].routes[i].root;
             default_index = servers[0].routes[i].index_file;
             root_set = true;
             break;
        }
    }
    // Fallback if no specific root location found in the first server
    if (!root_set && !servers[0].routes.empty()) {
         server_root = servers[0].routes[0].root; // Or maybe leave the ServerConfig default?
         default_index = servers[0].routes[0].index_file;
    }
    // else leave ServerConfig defaults if first server has no routes
}

// --- Find Server/Route Logic ---

// Find server block matching listen address/port and Host header
const ServerBlock* ServerConfig::findServerBlock(const std::string& listen_host,
                                              unsigned short listen_port,
                                              const std::string& host_header) const {
    const ServerBlock* default_server_for_port = NULL;
    const ServerBlock* named_match = NULL;

    for (size_t i = 0; i < servers.size(); ++i) {
        const ServerBlock& server = servers[i];

        // Check if the server is listening on the requested port
        if (server.port != listen_port) {
            continue;
        }

        // Check if the server is listening on the correct host/IP address
        // Note: "0.0.0.0" should match any incoming address. '*' might also be used.
        if (server.host != "0.0.0.0" && server.host != "*" && server.host != listen_host) {
            continue;
        }

        // This server matches the listen address:port

        // If this is the first match for this port, it's the default for now
        if (default_server_for_port == NULL) {
            default_server_for_port = &server;
        }

        // Check if server_name matches the Host header
        if (!host_header.empty()) {
             for (size_t j = 0; j < server.server_names.size(); ++j) {
                 // Simple exact match for now. Add wildcard/regex support if needed.
                 if (server.server_names[j] == host_header) {
                     named_match = &server;
                     goto found_match; // Found the best possible match
                 }
             }
        }
    }

found_match:
    if (named_match) {
        return named_match; // Return specific server_name match
    }
    // Otherwise, return the first server that matched the listen address:port
    return default_server_for_port;
}

// Check if a route matches the requested path
bool ServerConfig::isRouteMatch(const RouteConfig& route, const std::string& path) const {
    // Use prefix matching: route.path must be a prefix of the request path
    if (path.rfind(route.path, 0) == 0) { // Check if path starts with route.path
        // Ensure it's a "complete" match (e.g., /images matches /images/foo.jpg
        // but not /images-backup/bar.png unless route.path ends with /)
        bool is_complete_match = (path.length() == route.path.length() || // Exact match
                                route.path[route.path.length() - 1] == '/' || // Route ends with /
                                path[route.path.length()] == '/'); // Next char in path is /
        return is_complete_match;
    }
    return false;
}

// Find route within a server block that best matches the request path
const RouteConfig* ServerConfig::findRoute(const ServerBlock* server,
                                      const std::string& path) const {
    if (!server) {
        return NULL;
    }

    const RouteConfig* best_match = NULL;
    size_t longest_match_len = 0;

    for (size_t i = 0; i < server->routes.size(); ++i) {
        const RouteConfig& route = server->routes[i];

        if (isRouteMatch(route, path)) {
            // Check if this match is longer than the current best match
            if (route.path.length() > longest_match_len) {
                longest_match_len = route.path.length();
                best_match = &route;
            }
        }
    }

    return best_match;
}

// Convenience function combining server and route finding
const RouteConfig* ServerConfig::findRouteForRequest(const std::string& listen_host,
                                                 unsigned short listen_port,
                                                 const std::string& host_header,
                                                 const std::string& path) const {
    const ServerBlock* server = findServerBlock(listen_host, listen_port, host_header);
    if (!server) {
        // No server block matched the listen address/port/host header
        return NULL;
    }

    return findRoute(server, path);
}