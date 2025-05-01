#include "webserv.hpp"

// Constructor - sets global defaults and a fallback server block
ServerConfig::ServerConfig()
    : port_(80),
      server_root_("./www"),
      default_index_("index.html"),
      max_request_body_size_(1024 * 1024),
      connection_timeout_seconds_(60) {
    set_default_mime_types();
    set_default_cgi_handlers();
    set_default_server_block();
}

void ServerConfig::set_default_server_block() {
    // Create a minimal default server block configuration
    // This will be used if loadFromFile fails or finds no servers_.
    // It will be cleared if loadFromFile succeeds.
    ServerBlock defaultServer;

    // Add a default root location to the default server
    RouteConfig defaultRoute;
    defaultRoute.path_ = "/";
    defaultRoute.root_ = this->server_root_;
    defaultRoute.index_file_ = this->default_index_;
    defaultRoute.allowed_methods_.push_back("POST");
    defaultRoute.allowed_methods_.push_back("GET");

    defaultServer.routes_.push_back(defaultRoute);
    servers_.push_back(defaultServer);  // Add this default server
}

// Set up default MIME types
void ServerConfig::set_default_mime_types() {
    mime_types_[".html"] = "text/html";
    mime_types_[".htm"] = "text/html";
    mime_types_[".css"] = "text/css";
    mime_types_[".js"] = "application/javascript";
    mime_types_[".json"] = "application/json";
    mime_types_[".xml"] = "application/xml";
    mime_types_[".txt"] = "text/plain";
    mime_types_[".jpg"] = "image/jpeg";
    mime_types_[".jpeg"] = "image/jpeg";
    mime_types_[".png"] = "image/png";
    mime_types_[".gif"] = "image/gif";
    mime_types_[".svg"] = "image/svg+xml";
    mime_types_[".ico"] = "image/x-icon";
    mime_types_[".pdf"] = "application/pdf";
    mime_types_[".zip"] = "application/zip";
    mime_types_[".gz"] = "application/gzip";
    mime_types_[".tar"] = "application/x-tar";
    // Add more as needed
}

// Set up default CGI handlers (paths might need adjustment based on system)
void ServerConfig::set_default_cgi_handlers() {
    cgi_executables_[".php"] = "/usr/bin/php-cgi";  // Example path
    cgi_executables_[".py"] = "/usr/bin/python3";   // Example path
    cgi_executables_[".pl"] = "/usr/bin/perl";      // Example path
    // Add more as needed
}

// Helper to get file extension including the dot
std::string ServerConfig::get_file_extension(
    const std::string& filename) const {
    size_t pos = filename.find_last_of('.');
    if (pos != std::string::npos) {
        return filename.substr(pos);
    }
    return "";  // No extension found
}

// Get MIME type for file based on extension
std::string ServerConfig::get_mime_type(const std::string& filename) const {
    std::string ext = get_file_extension(filename);
    if (!ext.empty()) {
        std::map<std::string, std::string>::const_iterator it =
            mime_types_.find(ext);
        if (it != mime_types_.end()) {
            return it->second;
        }
    }
    return "application/octet-stream";  // Default MIME type
}

// Check if a file extension is associated with a CGI handler
bool ServerConfig::is_cgi_extension(const std::string& filename) const {
    std::string ext = get_file_extension(filename);
    return !ext.empty() && cgi_executables_.count(ext);
}

// Get CGI handler for a file extension
std::string ServerConfig::get_cgi_handler(const std::string& filename) const {
    std::string ext = get_file_extension(filename);
    if (!ext.empty()) {
        std::map<std::string, std::string>::const_iterator it =
            cgi_executables_.find(ext);
        if (it != cgi_executables_.end()) {
            return it->second;
        }
    }
    return "";  // No CGI handler found
}

// Helper to skip empty lines and lines starting with '#'
bool ServerConfig::skip_empty_and_comment_lines(std::istream& stream,
                                                std::string& line) {
    while (std::getline(stream, line)) {
        size_t first_char_pos = line.find_first_not_of(" \t");
        if (first_char_pos != std::string::npos &&
            line[first_char_pos] != '#') {
            return true;  // Found a non-empty, non-comment line
        }
        // Continue if line is empty or a comment
    }
    return false;  // Reached end of stream
}

// Trim whitespace from the beginning and end of a string
std::string ServerConfig::trim_string(const std::string& str) {
    if (str.empty()) return str;

    size_t first = str.find_first_not_of(" \t");
    if (first == std::string::npos) return "";

    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, last - first + 1);
}

// Handle CGI extension directives (cgi_ext.php style)
void ServerConfig::handle_cgi_ext_directive(
    std::map<std::string, std::string>& directives, const std::string& line) {
    size_t dot_pos = line.find("cgi_ext.");
    if (dot_pos == std::string::npos) return;

    std::string key = "cgi_extension";
    std::string ext = line.substr(dot_pos + 7);  // Skip "cgi_ext."

    // Remove semicolon and whitespace
    size_t semicolon_pos = ext.find(';');
    if (semicolon_pos != std::string::npos) {
        ext = ext.substr(0, semicolon_pos);
    }

    // Trim any remaining whitespace
    ext = trim_string(ext);

    // Make sure extension starts with a dot
    if (!ext.empty() && ext[0] != '.') {
        ext = "." + ext;
    }

    directives[key] = ext;
}

// Handle key-only directives (with or without semicolon)
void ServerConfig::handle_key_only_directive(
    std::map<std::string, std::string>& directives,
    const std::string& trimmed_line, size_t first_char) {
    std::string key = trimmed_line.substr(first_char);
    size_t semi_colon = key.find(';');
    if (semi_colon != std::string::npos) {
        key = key.substr(0, semi_colon);
    }

    // Trim any trailing whitespace
    key = trim_string(key);

    if (!key.empty()) directives[key] = "";  // Store key with empty value
}

// Handle key-value directives
void ServerConfig::handle_key_value_directive(
    std::map<std::string, std::string>& directives,
    const std::string& trimmed_line, size_t first_char, size_t first_space) {
    std::string key = trimmed_line.substr(first_char, first_space - first_char);

    size_t value_start = trimmed_line.find_first_not_of(" \t", first_space);
    if (value_start == std::string::npos) {
        // Key without a value, potentially ending in ;
        size_t semi_colon = key.find(';');
        if (semi_colon != std::string::npos) {
            key = key.substr(0, semi_colon);
        }

        key = trim_string(key);

        if (!key.empty()) directives[key] = "";
        return;
    }

    // Extract value, trim trailing whitespace and optional semicolon
    std::string value = trimmed_line.substr(value_start);
    size_t semi_colon = value.find(';');
    if (semi_colon != std::string::npos) {
        value = value.substr(0, semi_colon);
    }

    value = trim_string(value);

    directives[key] = value;
}

// Parse a single line into key-value pairs 
void ServerConfig::parse_line(const std::string& line, std::map<std::string, std::string>& directives) {
    std::string trimmed_line = line;
    size_t comment_pos = trimmed_line.find('#');
    if (comment_pos != std::string::npos) {
        trimmed_line = trimmed_line.substr(0, comment_pos);
    }

    size_t first_char = trimmed_line.find_first_not_of(" \t");
    if (first_char == std::string::npos)
        return;  // Line is empty or whitespace only

    // Special case for CGI directives with dot syntax (cgi_ext.php)
    if (trimmed_line.find("cgi_ext.") != std::string::npos) {
        handle_cgi_ext_directive(directives, trimmed_line);
        return;
    }

    size_t first_space = trimmed_line.find_first_of(" \t", first_char);
    if (first_space == std::string::npos) {
        // Maybe a directive with no value
        handle_key_only_directive(directives, trimmed_line, first_char);
        return;
    }

    // Handle standard key-value directive
    handle_key_value_directive(directives, trimmed_line, first_char, first_space);
}

// Parse listen directive for server block
bool ServerConfig::parse_listen_directive(ServerBlock& server, const std::string& value) {
    size_t colon_pos = value.find(':');
    if (colon_pos != std::string::npos) {
        server.host_ = value.substr(0, colon_pos);
        std::istringstream ss(value.substr(colon_pos + 1));
        if (!(ss >> server.port_)) {
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
            if (!(ss >> server.port_)) {
                std::cerr << "Error: Invalid port number in listen directive: " << value << std::endl;
                return false;
            }
            // Keep default host
        } else {
            server.host_ = value;
            // Keep default port
        }
    }
    return true;
}

// Parse server_name directive
void ServerConfig::parse_server_name_directive(ServerBlock& server, const std::string& value) {
    std::istringstream ss(value);
    std::string name;
    while (ss >> name) {
        if (!name.empty()) {
            server.server_names_.push_back(name);
        }
    }
}

// Parse client_max_body_size directive
bool ServerConfig::parse_max_body_size_directive(ServerBlock& server, const std::string& value) {
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

    if (unit == 'k')
        size_val *= 1024;
    else if (unit == 'm')
        size_val *= (1024 * 1024);
    else if (unit == 'g')
        size_val *= (1024 * 1024 * 1024);
    server.max_body_size_ = size_val;
    return true;
}

// Parse error_page directive
bool ServerConfig::parse_error_page_directive(ServerBlock& server, const std::string& value) {
    std::istringstream ss(value);
    int code;
    if (!(ss >> code)) {
        std::cerr << "Error: Invalid status code in error_page directive: " << value << std::endl;
        return false;
    }

    // The rest of the line is the path
    std::string page_path;
    std::getline(ss, page_path);
        page_path = trim_string(page_path);
        
        if (!page_path.empty()) {
            server.error_pages_[code] = page_path;
        } else {
            std::cerr << "Error: Missing path in error_page directive: " << value << std::endl;
            return false;
        }

    return true;
}

// Check if a location block has a valid path
bool ServerConfig::validate_location_path(const std::string& line,
                                          std::string& path) {
    size_t loc_pos = line.find("location");
    size_t path_start =
        line.find_first_not_of(" \t", loc_pos + 8);  // Skip "location "
    size_t path_end = std::string::npos;

    if (path_start != std::string::npos) {
        path_end = line.find_first_of(" \t{", path_start);
        if (path_end != std::string::npos) {
            path = line.substr(path_start, path_end - path_start);
        } else {
            path = line.substr(
                path_start);  // Path until end of line if no space/brace
        }
    }

    if (path.empty()) {
        std::cerr << "Error: Invalid location directive - missing path: "
                  << line << std::endl;
        return false;
    }
    return true;
}

// Find opening brace for location block
bool ServerConfig::find_opening_brace(std::istream& config_file,
                                      std::string& current_line,
                                      size_t& brace_pos,
                                      const std::string& context_label) {
    brace_pos = current_line.find('{');

    if (brace_pos == std::string::npos) {
        if (!skip_empty_and_comment_lines(config_file, current_line)) {
            std::cerr << "Error: Expected '{' after " << context_label
                      << ", but reached end of file." << std::endl;
            return false;
        }
        brace_pos = current_line.find('{');
    }

    if (brace_pos == std::string::npos) {
        std::cerr << "Error: Expected '{' to start " << context_label
                  << " block, found: " << current_line << std::endl;
        return false;
    }

    return true;
}


// Parse return (redirect) directive in location block
bool ServerConfig::parse_return_directive(RouteConfig& route,
                                          const std::string& value) {
    std::istringstream ss(value);
    int code = 0;
    std::string url;
    if (ss >> code) {
        // The rest is the URL
        size_t url_start = value.find_first_not_of(
            " \t", ss.tellg() == std::streampos(-1) ? 0 : (size_t)ss.tellg());
        if (url_start != std::string::npos) {
            url = value.substr(url_start);
        } else {
            std::cerr << "Error: Missing URL in return directive for location '" << route.path_ << "'" << std::endl;
            return false;
        }
        route.redirect_code_ = code;
        route.redirect_url_ = url;
        return true;
    } else {
        std::cerr << "Error: Invalid code in return directive for location '" << route.path_ << "'" << std::endl;
        return false;
    }
}

// Parse allow_methods or limit_except directive in location block
void ServerConfig::parse_methods_directive(RouteConfig& route,
                                           const std::string& value) {
    route.allowed_methods_.clear();  // Override any defaults if specified
    std::istringstream ss(value);
    std::string method;
    while (ss >> method) {
        if (!method.empty()) {
            route.allowed_methods_.push_back(method);
        }
    }
    // Optional: Validate methods (GET, POST, PUT, DELETE, HEAD, etc.)
}

// Process a server-level directive 
bool ServerConfig::process_server_directive(ServerBlock& server, const std::string& key, const std::string& value) {
    if (key == "listen") {
        return parse_listen_directive(server, value); 
    } else if (key == "server_name") {
        parse_server_name_directive(server, value); 
        return true;
    } else if (key == "client_max_body_size") {
        return parse_max_body_size_directive(server, value); 
    } else if (key == "error_page") {
        return parse_error_page_directive(server, value); 
    }
    else {
        std::cerr << "Warning: Unknown server directive '" << key << "' found. Ignoring." << std::endl;
        return true; // Treat unknown directives as non-fatal warnings
    }
}

// Process a location-level directive 
bool ServerConfig::process_location_directive(RouteConfig& route, const std::string& key, const std::string& value) {
    if (key == "root") {
        route.root_ = value; 
    } else if (key == "index") {
        std::istringstream ss(value);
        std::string index_file;
        if (ss >> index_file && !index_file.empty()) {
            route.index_file_ = index_file;
        } else {
             std::cerr << "Warning: Empty or invalid index directive value in location '" << route.path_ << "'" << std::endl;
        }
    } else if (key == "autoindex") {
        route.directory_listing_ = (value == "on"); // Inline boolean conversion
    } else if (key == "cgi_pass" || key == "fastcgi_pass") {
        route.cgi_handler_ = value; 
    } else if (key == "cgi_extension") {
        route.cgi_extension_ = value;
        if (!route.cgi_extension_.empty() && route.cgi_extension_[0] != '.') {
            route.cgi_extension_ = "." + route.cgi_extension_;
        }
    } else if (key == "upload_enable" || key == "accept_uploads") {
         route.uploads_enabled_ = (value == "on" || value == "true" || value == "yes" || value == "1"); // Inline boolean conversion
    } else if (key == "upload_store" || key == "upload_dir" || key == "upload_path") {
         route.upload_dir_ = value; 
    }
    // --- Handle complex directives via specific parsers ---
    else if (key == "return") {
        return parse_return_directive(route, value); 
    } else if (key == "allow_methods" || key == "limit_except") {
        parse_methods_directive(route, value); 
    }
    // --- Handle unknown directives ---
    else {
        std::cerr << "Warning: Unknown location directive '" << key << "' in location '" << route.path_ << "'. Ignoring." << std::endl;
    }

    return true; // Assume success unless a complex parser returns false
}

// Parse a location block (expects stream position after '{')
bool ServerConfig::parse_location_block(std::istream& config_file,
                                        RouteConfig& route) {
    std::string line;
    std::map<std::string, std::string> directives;

    while (skip_empty_and_comment_lines(config_file, line)) {
        if (line.find('}') != std::string::npos) {
            // End of location block
            // Validation: check if essential fields like root are set?
            if (route.root_.empty() && route.redirect_url_.empty()) {
                std::cerr
                    << "Warning: Location block '" << route.path_
                    << "' defined without a 'root' or 'redirect' directive. "
                    << "Behavior might be undefined." << std::endl;
                // Depending on strictness, could return false here.
            }
            return true;
        }

        // --- Location Directive ---
        directives.clear();
        parse_line(line, directives);

        if (!directives.empty()) {
            std::map<std::string, std::string>::const_iterator it =
                directives.begin();
            std::string key = it->first;
            std::string value = it->second;

            // Process the directive
            if (!process_location_directive(route, key, value)) {
                return false;  // Stop on error
            }
        }
    }

    std::cerr << "Error: Reached end of file while parsing location block '"
              << route.path_ << "'. Missing '}'." << std::endl;
    return false;
}

// Parse a server block (expects the stream position after '{')
bool ServerConfig::parse_server_block(std::istream& config_file) {
    ServerBlock current_server;  // Uses ServerBlock constructor defaults initially
    std::string line;
    std::map<std::string, std::string> directives;

    size_t brace_pos;
    if (!find_opening_brace(config_file, line, brace_pos, "server block")) {
        return false;
    }

    while (skip_empty_and_comment_lines(config_file, line)) {
        if (line.find('}') != std::string::npos) {
            // End of server block
            servers_.push_back(current_server);  // Add the parsed server
            return true;
        }

        // Check for location block
        size_t loc_pos = line.find("location");
        size_t first_char = line.find_first_not_of(" \t");

        if (loc_pos != std::string::npos && loc_pos == first_char) {
            // --- Location Block ---
            std::string path;
            if (!validate_location_path(line, path)) {
                return false;
            }

            // Find opening brace '{'
            size_t brace_pos;
            if (!find_opening_brace(config_file, line, brace_pos, "location path")) {
                return false;
            }

            RouteConfig current_route;
            current_route.path_ = path;

            if (!parse_location_block(config_file, current_route)) {
                return false;  // Propagate error from location parsing
            }
            current_server.routes_.push_back(
                current_route);  // Add parsed route

        } else {
            // --- Server Directive ---
            directives.clear();
            parse_line(line, directives);

            if (!directives.empty()) {
                std::map<std::string, std::string>::const_iterator it =
                    directives.begin();
                std::string key = it->first;
                std::string value = it->second;

                // Process the server directive
                if (!process_server_directive(current_server, key, value)) {
                    return false;  // Stop on error
                }
            }
        }
    }

    // If loop finishes because EOF was reached before finding '}'
    std::cerr
        << "Error: Reached end of file while parsing server block. Missing '}'."
        << std::endl;
    return false;
}

// Load configuration from file
bool ServerConfig::load_from_file(const std::string& filename) {
    std::ifstream config_file(filename.c_str());
    if (!config_file.is_open()) {
        std::cerr << "Error: Failed to open config file: " << filename << std::endl;
        return false;
    }

    servers_.clear();
    std::string line;

    try {
        while (skip_empty_and_comment_lines(config_file, line)) {
            size_t server_pos = line.find("server");
            size_t first_char = line.find_first_not_of(" \t");

            if (server_pos != std::string::npos && server_pos == first_char) {
                if (!parse_server_block(config_file)) {
                    servers_.clear();
                    return false;
                }
            } else {
                std::cerr << "Error: Expected 'server' directive at top level, found: " << line << std::endl;
                servers_.clear();
                return false;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing config file: " << e.what() << std::endl;
        servers_.clear();
        return false;
    }

    return handle_empty_server_config(filename);
}

// Handle empty server configuration
bool ServerConfig::handle_empty_server_config(const std::string& filename) {
    // If parsing succeeded but no server blocks were defined
    if (servers_.empty()) {
        std::cerr << "Warning: No server blocks defined in config file: "
                  << filename << ". Using default server." << std::endl;
        ServerConfig default_config;  // Create a temporary default config
        servers_ = default_config.servers_;  // Copy its default server setup
        // We don't return false here, as the file was read, just empty/invalid structure.
    }

    // Update ServerConfig members from the *first* parsed server block
    if (!servers_.empty()) {
        update_config_from_first_server();
    }

    return true;
}

// Update config from the first server
void ServerConfig::update_config_from_first_server() {
    port_ = servers_[0].port_;
    max_request_body_size_ = servers_[0].max_body_size_;

    // Try to get root/index from the first server's root location if possible
    bool root_set = false;
    for (size_t i = 0; i < servers_[0].routes_.size(); ++i) {
        if (servers_[0].routes_[i].path_ == "/") {
            server_root_ = servers_[0].routes_[i].root_;
            default_index_ = servers_[0].routes_[i].index_file_;
            root_set = true;
            break;
        }
    }
    // Fallback if no specific root location found in the first server
    if (!root_set && !servers_[0].routes_.empty()) {
        server_root_ = servers_[0]
                           .routes_[0]
                           .root_;  // Or maybe leave the ServerConfig default?
        default_index_ = servers_[0].routes_[0].index_file_;
    }
    // else leave ServerConfig defaults if first server has no routes_
}

// --- Find Server/Route Logic ---

// Find server block matching listen address/port and Host header
const ServerBlock* ServerConfig::find_server_block(
    const std::string& listen_host, unsigned short listen_port,
    const std::string& host_header) const {
    const ServerBlock* default_server_for_port = NULL;

    for (size_t i = 0; i < servers_.size(); ++i) {
        const ServerBlock& server = servers_[i];

        // Check if the server is listening on the requested port
        if (server.port_ != listen_port) {
            continue;
        }

        // Check if the server is listening on the correct host/IP address
        // Note: "0.0.0.0" should match any incoming address. 
        // '*' might also be used.
        if (server.host_ != "0.0.0.0" && server.host_ != "*" && server.host_ != listen_host) {
            continue;
        }

        // This server matches the listen address:port

        // If this is the first match for this port, it's the default for now
        if (default_server_for_port == NULL) {
            default_server_for_port = &server;
        }

        // Check if server_name matches the Host header
        if (!host_header.empty()) {
            for (size_t j = 0; j < server.server_names_.size(); ++j) {
                if (server.server_names_[j] == host_header) {
                    return &server;
                }
            }
        }
    }
    return default_server_for_port;
}

// Check if a route matches the requested path
bool ServerConfig::is_route_match(const RouteConfig& route,
                                  const std::string& path) const {
    // Use prefix matching: route.path must be a prefix of the request path
    if (path.rfind(route.path_, 0) ==
        0) {  // Check if path starts with route.path
        // Ensure it's a "complete" match (e.g., /images matches /images/foo.jpg
        // but not /images-backup/bar.png unless route.path ends with /)
        bool is_complete_match =
            (path.length() == route.path_.length() ||  // Exact match
             route.path_[route.path_.length() - 1] ==
                 '/' ||                           // Route ends with /
             path[route.path_.length()] == '/');  // Next char in path is /
        return is_complete_match;
    }
    return false;
}

// Find route within a server block that best matches the request path
const RouteConfig* ServerConfig::find_route(const ServerBlock* server,
                                            const std::string& path) const {
    if (!server) {
        return NULL;
    }

    const RouteConfig* best_match = NULL;
    size_t longest_match_len = 0;

    for (size_t i = 0; i < server->routes_.size(); ++i) {
        const RouteConfig& route = server->routes_[i];

        if (is_route_match(route, path)) {
            // Check if this match is longer than the current best match
            if (route.path_.length() > longest_match_len) {
                longest_match_len = route.path_.length();
                best_match = &route;
            }
        }
    }

    return best_match;
}

// Convenience function combining server and route finding
const RouteConfig* ServerConfig::find_route_for_request(
    const std::string& listen_host, unsigned short listen_port,
    const std::string& host_header, const std::string& path) const {
    const ServerBlock* server =
        find_server_block(listen_host, listen_port, host_header);
    if (!server) {
        // No server block matched the listen address/port/host header
        return NULL;
    }

    return find_route(server, path);
}