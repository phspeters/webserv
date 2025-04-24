#include "ServerConfig.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring> // For isdigit in parsing listen
#include <cstddef> // For size_t
#include <stdexcept> // For runtime_error in parsing

// Constructor - sets global defaults and a fallback server block
ServerConfig::ServerConfig()
    : port(80), // Default port if no server blocks are loaded or first block doesn't specify
      server_root("./www"), // Default root if no server blocks loaded...
      default_index("index.html"), // Default index if no server blocks loaded...
      max_request_body_size(1024 * 1024), // 1MB default if no server blocks loaded...
      connection_timeout_seconds(60) { // Default timeout

    setDefaultMimeTypes();
    setDefaultCgiHandlers();

    // Create a minimal default server block configuration
    // This will be used if loadFromFile fails or finds no servers.
    // It will be cleared if loadFromFile succeeds.
    ServerBlock defaultServer;
    // Defaults from ServerBlock constructor are used (0.0.0.0:80, 1MB)

    // Add a default root location to the default server
    RouteConfig defaultRoute;
    defaultRoute.path = "/";
    defaultRoute.root = this->server_root; // Use ServerConfig's initial default
    defaultRoute.index_file = this->default_index; // Use ServerConfig's initial default
    defaultRoute.allowed_methods.push_back("GET"); // Sensible minimum
    defaultRoute.allowed_methods.push_back("HEAD");
    // directory_listing defaults to false
    // uploads_enabled defaults to false

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

    // If parsing succeeded but no server blocks were defined
    if (servers.empty()) {
        std::cerr << "Warning: No server blocks defined in config file: " << filename << ". Using default server." << std::endl;
        // Re-add the default server configuration if parsing finished but found nothing
        ServerConfig default_config; // Create a temporary default config
        servers = default_config.servers; // Copy its default server setup
        // We *don't* return false here, as the file was read, just empty/invalid structure.
    }

    // Update ServerConfig members from the *first* parsed server block
    // This preserves the original behavior, though its utility is debatable.
    if (!servers.empty()) {
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
        } else if (!root_set && servers[0].routes.empty()) {
             // Leave ServerConfig defaults if first server has no routes
        }
    }

    return true;
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

// Parse a single line into key-value pairs (improved parsing)
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
    size_t dot_pos = trimmed_line.find("cgi_ext.");
    if (dot_pos != std::string::npos) {
        std::string key = "cgi_extension";
        std::string ext = trimmed_line.substr(dot_pos + 7); // Skip "cgi_ext."
        
        // Remove semicolon and whitespace
        size_t semicolon_pos = ext.find(';');
        if (semicolon_pos != std::string::npos) {
            ext = ext.substr(0, semicolon_pos);
        }
        
        // Trim any remaining whitespace
        size_t ext_end = ext.find_last_not_of(" \t");
        if (ext_end != std::string::npos) {
            ext = ext.substr(0, ext_end + 1);
        }
        
        // Make sure extension starts with a dot
        if (!ext.empty() && ext[0] != '.') {
            ext = "." + ext;
        }
        
        directives[key] = ext;
        return;
    }

    size_t first_space = trimmed_line.find_first_of(" \t", first_char);
    if (first_space == std::string::npos) {
        // Maybe a directive with no value? Handle as needed or ignore.
        // Example: `worker_processes;` could be just key "worker_processes"
        std::string key = trimmed_line.substr(first_char);
        size_t semi_colon = key.find(';');
        if (semi_colon != std::string::npos) {
             key = key.substr(0, semi_colon);
        }
        
        // Trim any trailing whitespace
        size_t key_end = key.find_last_not_of(" \t");
        if (key_end != std::string::npos) {
            key = key.substr(0, key_end + 1);
        }
        
        if (!key.empty()) directives[key] = ""; // Store key with empty value
        return;
    }

    std::string key = trimmed_line.substr(first_char, first_space - first_char);

    size_t value_start = trimmed_line.find_first_not_of(" \t", first_space);
    if (value_start == std::string::npos) {
        // Key without a value, potentially ending in ;
        size_t semi_colon = key.find(';');
        if (semi_colon != std::string::npos) {
            key = key.substr(0, semi_colon);
        }
        
        // Trim any trailing whitespace
        size_t key_end = key.find_last_not_of(" \t");
        if (key_end != std::string::npos) {
            key = key.substr(0, key_end + 1);
        }
        
        if (!key.empty()) directives[key] = "";
        return;
    }

    // Extract value, trim trailing whitespace and optional semicolon
    std::string value = trimmed_line.substr(value_start);
    size_t semi_colon = value.find(';');
    if (semi_colon != std::string::npos) {
        value = value.substr(0, semi_colon);
    }
    
    // Trim any trailing whitespace
    size_t value_end = value.find_last_not_of(" \t");
    if (value_end != std::string::npos) {
        value = value.substr(0, value_end + 1);
    }

    directives[key] = value;
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
            size_t path_start = line.find_first_not_of(" \t", loc_pos + 8); // Skip "location "
            size_t path_end = std::string::npos;
             size_t brace_pos = line.find('{', (path_start != std::string::npos ? path_start : loc_pos + 8));

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

            // Find opening brace '{' (can be on same or next line)
             if (brace_pos == std::string::npos) {
                 if (!skipEmptyAndCommentLines(config_file, line)) {
                      std::cerr << "Error: Expected '{' after location path, but reached end of file." << std::endl;
                      return false;
                 }
                 brace_pos = line.find('{');
             }

             if (brace_pos == std::string::npos) {
                  std::cerr << "Error: Expected '{' to start location block, found: " << line << std::endl;
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
                std::map<std::string, std::string>::const_iterator it = directives.begin(); // Only one pair expected from parseLine
                std::string key = it->first;
                std::string value = it->second;

                // Handle server directives
                if (key == "listen") {
                    size_t colon_pos = value.find(':');
                    if (colon_pos != std::string::npos) {
                        current_server.host = value.substr(0, colon_pos);
                        std::istringstream ss(value.substr(colon_pos + 1));
                        if (!(ss >> current_server.port)) {
                             std::cerr << "Error: Invalid port number in listen directive: " << value << std::endl; return false;
                        }
                    } else {
                        // Check if value is just a port or just a host/address
                        bool is_numeric_port = true;
                        for (size_t i = 0; i < value.length(); ++i) {
                            if (!isdigit(value[i])) { is_numeric_port = false; break; }
                        }
                        if (is_numeric_port) {
                            std::istringstream ss(value);
                             if (!(ss >> current_server.port)) {
                                 std::cerr << "Error: Invalid port number in listen directive: " << value << std::endl; return false;
                             }
                             // Keep default host "0.0.0.0"
                        } else {
                            current_server.host = value;
                            // Keep default port 80 (or whatever ServerBlock sets)
                        }
                    }
                } else if (key == "server_name") {
                    std::istringstream ss(value);
                    std::string name;
                    while (ss >> name) {
                        if (!name.empty()) { current_server.server_names.push_back(name); }
                    }
                } else if (key == "client_max_body_size") {
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
                          std::cerr << "Error: Invalid value for client_max_body_size: " << value << std::endl; return false;
                     }

                    if (unit == 'k') size_val *= 1024;
                    else if (unit == 'm') size_val *= (1024 * 1024);
                    else if (unit == 'g') size_val *= (1024 * 1024 * 1024);
                    current_server.max_body_size = size_val;

                } else if (key == "error_page") {
                    std::istringstream ss(value);
                    int code;
                    std::string page_path;
                    if (!(ss >> code)) {
                        std::cerr << "Error: Invalid status code in error_page directive: " << value << std::endl; return false;
                    }
                    // The rest of the line is the path
                    size_t path_start = value.find_first_not_of(" \t", ss.tellg() == std::streampos(-1) ? 0 : (size_t)ss.tellg());

                    if (path_start != std::string::npos) {
                         page_path = value.substr(path_start);
                         current_server.error_pages[code] = page_path;
                    } else {
                         std::cerr << "Error: Missing path in error_page directive: " << value << std::endl; return false;
                    }
                } else {
                     std::cerr << "Warning: Unknown server directive '" << key << "' found. Ignoring." << std::endl;
                }
            } 
        } 
    } 

    // If loop finishes because EOF was reached before finding '}'
    std::cerr << "Error: Reached end of file while parsing server block. Missing '}'." << std::endl;
    return false;
}

// Parse a location block (expects stream position after '{')
bool ServerConfig::parseLocationBlock(std::istream& config_file, RouteConfig& route) {
    std::string line;
    std::map<std::string, std::string> directives;

    // Set initial defaults based on server? Or just rely on RouteConfig constructor?
    // Let's assume RouteConfig constructor + parsed directives for now.

    while (skipEmptyAndCommentLines(config_file, line)) {
        if (line.find('}') != std::string::npos) {
            // End of location block
            // Validation: check if essential fields like root are set?
             if (route.root.empty() && route.redirect_url.empty()) {
                 std::cerr << "Warning: Location block '" << route.path << "' defined without a 'root' or 'redirect' directive. Behavior might be undefined." << std::endl;
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

            // Handle location directives
            if (key == "root") {
                route.root = value;
            } else if (key == "index") {
                // Parse index files (multiple files may be specified)
                std::istringstream ss(value);
                std::string index;
                ss >> index; // Just take the first one for now
                if (!index.empty()) {
                    route.index_file = index;
                }
            } else if (key == "autoindex") {
                route.directory_listing = (value == "on");
            } else if (key == "return") { // Using 'return' like Nginx for redirects
                 std::istringstream ss(value);
                 int code = 0;
                 std::string url;
                 if (ss >> code) {
                     // The rest is the URL
                     size_t url_start = value.find_first_not_of(" \t", ss.tellg() == std::streampos(-1) ? 0 : (size_t)ss.tellg());
                     if (url_start != std::string::npos) {
                         url = value.substr(url_start);
                     } else {
                          std::cerr << "Error: Missing URL in return directive for location '" << route.path << "'" << std::endl; return false;
                     }
                     route.redirect_code = code;
                     route.redirect_url = url;
                 } else {
                      std::cerr << "Error: Invalid code in return directive for location '" << route.path << "'" << std::endl; return false;
                 }
            } else if (key == "allow_methods" || key == "limit_except") { // Accept both common names
                route.allowed_methods.clear(); // Override any defaults if specified
                std::istringstream ss(value);
                std::string method;
                while (ss >> method) {
                    if (!method.empty()) { route.allowed_methods.push_back(method); }
                }
                 // Optional: Validate methods (GET, POST, PUT, DELETE, HEAD, etc.)
            } else if (key == "cgi_pass" || key == "fastcgi_pass") { // Support common directive names
                // This might involve more complex parsing (e.g., address:port or socket path)
                // For now, assuming it's just the handler path/identifier
                route.cgi_handler = value;
                 // You might need another directive like 'cgi_param' or 'fastcgi_param'
                 // And maybe 'cgi_index' or 'fastcgi_index'
                 // Also, you likely need to infer the cgi_extension or have it explicitly set
                 // This example keeps it simple.
            } else if (key == "cgi_extension") { // Explicit CGI extension if needed
                 route.cgi_extension = value;
            }
            else if (key == "upload_enable" || key == "accept_uploads") { // Handle both directive names
                route.uploads_enabled = (value == "on" || value == "true" || value == "yes" || value == "1");
            } else if (key == "upload_store" || key == "upload_dir" || key == "upload_path") { // Handle various naming options
                route.upload_dir = value;
            } else {
                std::cerr << "Warning: Unknown location directive '" << key << "' in location '" << route.path << "'. Ignoring." << std::endl;
            }
        } // end if !directives.empty()
    } // end while loop reading lines

    std::cerr << "Error: Reached end of file while parsing location block '" << route.path << "'. Missing '}'." << std::endl;
    return false;
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

        // Use prefix matching: route.path must be a prefix of the request path
        if (path.rfind(route.path, 0) == 0) { // Check if path starts with route.path
            // Ensure it's a "complete" match (e.g., /images matches /images/foo.jpg
            // but not /images-backup/bar.png unless route.path ends with /)
             bool is_complete_match = (path.length() == route.path.length() || // Exact match
                                       route.path[route.path.length() - 1] == '/' || // Route ends with /
                                       path[route.path.length()] == '/'); // Next char in path is /

            if (is_complete_match) {
                // Check if this match is longer than the current best match
                if (route.path.length() > longest_match_len) {
                    longest_match_len = route.path.length();
                    best_match = &route;
                }
            }
        }
    }

     // If no specific location matched, maybe return a default?
     // Nginx implicitly has a root location "/". If your parsing guarantees
     // a "/" location exists (or you add one if missing), you might not need this.
     // If best_match is still NULL here, it means no location block applied.

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