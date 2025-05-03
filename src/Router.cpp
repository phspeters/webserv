#include "webserv.hpp"

Router::Router(const ServerConfig& config) : config_(config) {}

Router::~Router() {
    // Clean up owned components
}

IHandler* Router::route(const HttpRequest* req) {

    // Example locations from Config

    // location /test1 {
    //     root /content; One root - mandatory
    //     autoindex off; One autoindex - default off
    //     allowed_methods GET POST DELETE; Less then 3 methods - default GET POST DELETE 
    //     index index.html; // One index redirect Onde redirect - default "" 
    //     cgi off; // One cgi - default off cgi_ext .py .php .sh; If cgi is on, one or more cgi_ext with .py .php .sh
    // }

    // location /test2 {
    //     root /content; One root - mandatory
    //     autoindex off; One autoindex - default off
    //     allowed_methods GET POST DELETE; Less then 3 methods - default GET POST DELETE 
    //     index index.html; // One index redirect Onde redirect - default "" 
    //     cgi off; // One cgi - default off cgi_ext .py .php .sh; If cgi is on, one or more cgi_ext with .py .php .sh
    // }

    // Example request:

    // GET /index.html HTTP/1.1
    // Host: localhost
    // Connection: keep-alive

    // Extract request information
    // req->method_;
    // req->uri_; /index.html 
    // req->version_;
    // req->headers_;
    // req->body_;

    // Parse URI
    std::string path = "";
    std::string filename = "";
    std::string extension = "";
    std::string query = "";

    // Separate the query string if exists
    std::string path_part;
    size_t query_pos = req->uri_.find('?');
    if (query_pos != std::string::npos) {
        path_part = req->uri_.substr(0, query_pos);
        query = req->uri_.substr(query_pos + 1); // Everything after the '?'
    } else {
        path_part = req->uri_;
    }

    // Extract the path and filename
    size_t last_slash_pos = path_part.find_last_of('/');

    if (last_slash_pos != std::string::npos) {
        // If the URI ends with a slash, there's no explicit filename
        if (last_slash_pos == path_part.length() - 1) {
            path = path_part; // Entire path with trailing slash
            filename = ""; // No explicit filename in request - DOUBLE CHECK for default index.html
        } else {
            path = path_part.substr(0, last_slash_pos + 1); // Include the trailing slash
            filename = path_part.substr(last_slash_pos + 1);
        }
    } else {
        // No slash in the URI (unusual but possible) - it is technically a valid URI
        path = "/";
        filename = path_part; // The entire URI is the filename
    }

    // Extract file extension if present
    if (!filename.empty()) {
        size_t dot_pos = filename.find_last_of('.');
        if (dot_pos != std::string::npos) {
            extension = filename.substr(dot_pos); // Include the dot
        }
    }

    // Find the matching location block
    std::vector<LocationConfig>::const_iterator it;
    LocationConfig* location_match = NULL;
    std::string longest_match_path = "";

    // Find the best matching location (longest prefix match)
    for (it = config_.locations.begin(); it != config_.locations.end(); ++it) {
        if (path_part.find(it->path) == 0) {
            // This location matches - check if it's better than our current match
            if (location_match == NULL || it->path.length() > longest_match_path.length()) {
                location_match = &(*it);
                longest_match_path = it->path;
            }
        }
    }

    if (location_match) {
        // We found a matching location
        
        // Check if the method is allowed
        std::vector<std::string> allowed_methods = location_match->directives.at("allowed_methods");
        if (std::find(allowed_methods.begin(), allowed_methods.end(), req->method_) == allowed_methods.end()) {
            // Method not allowed
            std::cerr << "Method " << req->method_ << " not allowed for location " 
                    << location_match->path << std::endl;
            return nullptr; // Return Method Not Allowed handler
        }
        
        // Get the root directory from the matched location
        std::string rootDir = location_match->directives.at("root")[0];
        
        // Build the full filesystem path
        std::string relativePath;
        std::string fullPath;
        
        // Calculate the path relative to the location
        if (path_part == location_match->path || path_part == location_match->path + "/") {
            // Requesting exactly the location with no extra path
            relativePath = "/";
        } else {
            // Remove the location prefix to get the relative path
            relativePath = path_part.substr(location_match->path.length());
            if (relativePath.empty() || relativePath[0] != '/') {
                relativePath = "/" + relativePath;
            }
        }
        
        // If no filename in the request and path ends with slash, use index file
        if (filename.empty() && relativePath.back() == '/') {
            std::string indexFile = location_match->directives.at("index")[0];
            fullPath = rootDir + relativePath + indexFile;
            filename = indexFile;
            
            // Extract extension from index file
            size_t dot_pos = indexFile.find_last_of('.');
            if (dot_pos != std::string::npos) {
                extension = indexFile.substr(dot_pos);
            }
        } else {
            fullPath = rootDir + relativePath;
        }
        
        // Check if the path exists in the filesystem
        struct stat path_stat;
        if (stat(fullPath.c_str(), &path_stat) != 0) {
            // Path doesn't exist
            std::cerr << "File not found: " << fullPath << std::endl;
            return nullptr; // Return 404 handler
        }
        
        // Check if it's a directory
        if (S_ISDIR(path_stat.st_mode)) {
            // It's a directory
            
            // If the request didn't end with a slash, redirect to add one
            if (path_part.back() != '/') {
                // Return a redirect handler
                return new RedirectHandler(path_part + "/");
            }
            
            // Try to serve the index file
            std::string indexFile = location_match->directives.at("index")[0];
            std::string indexPath = fullPath + "/" + indexFile;
            
            if (stat(indexPath.c_str(), &path_stat) != 0) {
                // Index file doesn't exist
                
                // Check if autoindex is enabled
                if (location_match->directives.at("autoindex")[0] == "on") {
                    // Return directory listing
                    return new DirectoryListingHandler(fullPath);
                } else {
                    // Autoindex is off - return 403 Forbidden
                    return nullptr; // Return 403 handler
                }
            }
            
            // Index file exists, serve it
            fullPath = indexPath;
            filename = indexFile;
            
            // Update extension based on index file
            size_t dot_pos = indexFile.find_last_of('.');
            if (dot_pos != std::string::npos) {
                extension = indexFile.substr(dot_pos);
            }
        }
        
        ResponseWriter writer(config_);
        
        // Check if CGI is enabled and file extension matches a CGI extension
        std::string cgi = location_match->directives.at("cgi")[0];
        if (cgi == "on" && !extension.empty()) {
            std::vector<std::string> cgiExts = location_match->directives.at("cgi_ext");
            if (std::find(cgiExts.begin(), cgiExts.end(), extension) != cgiExts.end()) {
                // It's a CGI script with matching extension
                return new CgiHandler(fullPath, writer);
            }
        }
        // Not a CGI script or CGI disabled - serve as static file
        return new StaticFileHandler(fullPath, writer);
        } else {
            // No matching location found
            std::cerr << "No matching location for URI: " << req->uri_ << std::endl;
            return nullptr; // Return 404 handler
        }

    // Check if the request uri matches one of the location paths
    // std::vector<LocationConfig>::const_iterator it;
    // for (it = config_.locations.begin(); it != config_.locations.end(); ++it) {
    //     if (req->uri_.find(it->path) == 0) {  // Uri matches location path
    //         // Check if the method is allowed
    //         std::vector<std::string> allowed_methods = it->directives.at("allowed_methods");
    //         if (std::find(allowed_methods.begin(), allowed_methods.end(), req->method_) != allowed_methods.end()) {
    //             ResponseWriter writer(config_);
    //             // Check if CGI is enabled   
    //             std::string cgi = it->directives.at("cgi")[0];
    //             if (cgi == "on") {
    //                 return new CgiHandler(config_, writer);
    //             }
    //             return new StaticFileHandler(config_, writer);
    //         } else {
    //             std::cerr << "Method not allowed for this location"
    //                       << std::endl;
    //         }
    //     } else {
    //         std::cerr << "Uri does not match location path" << std::endl;
    //     }
    // }
    return nullptr;  // No suitable handler found
}
