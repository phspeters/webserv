#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

#include <map>
#include <string>
#include <vector>

// Holds the configuration settings for the server.
// Typically loaded once at startup.
struct ServerConfig {
    //--------------------------------------
    // Configuration Members (Examples)
    //--------------------------------------
    unsigned short port;        // Port to listen on
    std::string server_root;    // Document root for static files
    std::string default_index;  // Default file to serve for directory requests (e.g., "index.html")
    size_t max_request_body_size;    // Max allowed size for request bodies
    int connection_timeout_seconds;  // Timeout for inactive connections

    // CGI configuration
    std::map<std::string, std::string> cgi_executables;  // Maps URI path/extension to CGI script path (e.g., ".php" -> "/usr/bin/php-cgi")
    std::string cgi_default_handler;  // Default handler if map doesn't match

    // MIME Types
    std::map<std::string, std::string> mime_types;  // Maps file extensions to MIME types

    //--------------------------------------
    // Constructor / Destructor
    //--------------------------------------
    ServerConfig();  // Constructor might set defaults or trigger loading
    ~ServerConfig() {}  // Default destructor likely okay

    //--------------------------------------
    // Methods (Declarations)
    //--------------------------------------
    bool loadFromFile(const std::string& filename);  // Declaration for loading logic (impl in .cpp)
    std::string getMimeType(const std::string& filename) const;  // Declaration (impl in .cpp)

   private:
    void setDefaultMimeTypes();  // Helper for constructor (impl in .cpp)

    // Usually only have one config; prevent copying if it simplifies ownership.
    ServerConfig(const ServerConfig&);
    ServerConfig& operator=(const ServerConfig&);

};  // struct ServerConfig

#endif  // SERVERCONFIG_HPP
