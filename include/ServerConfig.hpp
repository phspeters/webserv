#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

#include <string>
#include <vector>
#include <map>
#include <istream> 
#include "RouteConfig.hpp"
#include "ServerBlock.hpp"

class ServerConfig {
public:
    // Configuration Members (Primarily initial defaults, may be updated by first server block)
    unsigned short port;
    std::string server_root;
    std::string default_index;
    size_t max_request_body_size;
    int connection_timeout_seconds;

    // Global Handlers/Types
    std::map<std::string, std::string> mime_types;
    std::map<std::string, std::string> cgi_executables;

    // Parsed Server Blocks
    std::vector<ServerBlock> servers;

    // --- Public Methods ---
    ServerConfig(); // Constructor - sets defaults

    bool loadFromFile(const std::string& filename);

    // Find appropriate server/route for a request
    const ServerBlock* findServerBlock(const std::string& listen_host, // Host server is listening on
                                       unsigned short listen_port,   // Port server is listening on
                                       const std::string& host_header) const; // Host header from request

    const RouteConfig* findRoute(const ServerBlock* server,
                                 const std::string& path) const;

    const RouteConfig* findRouteForRequest(const std::string& listen_host,
                                           unsigned short listen_port,
                                           const std::string& host_header,
                                           const std::string& path) const;

    // MIME and CGI lookup
    std::string getMimeType(const std::string& filename) const;
    bool isCgiExtension(const std::string& filename) const;
    std::string getCgiHandler(const std::string& filename) const;


private:
    // --- Private Helper Methods ---
    void setDefaultMimeTypes();
    void setDefaultCgiHandlers();

    // Parsing Helpers
    std::string getFileExtension(const std::string& filename) const;
    void parseLine(const std::string& line, std::map<std::string, std::string>& configMap);
    bool skipEmptyAndCommentLines(std::istream& stream, std::string& line);
    bool parseServerBlock(std::istream& config_file);
    bool parseLocationBlock(std::istream& config_file, RouteConfig& route);
};

#endif // SERVERCONFIG_HPP