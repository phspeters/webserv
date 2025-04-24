#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

#include <istream>
#include <map>
#include <string>
#include <vector>

#include "RouteConfig.hpp"
#include "ServerBlock.hpp"

class ServerConfig {
   public:
    // Configuration Members
    unsigned short port;
    std::string server_name;
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
    ServerConfig();  // Constructor - sets defaults

    bool loadFromFile(const std::string& filename);

    // Find appropriate server/route for a request
    const ServerBlock* findServerBlock(
        const std::string& listen_host,         // Host server is listening on
        unsigned short listen_port,             // Port server is listening on
        const std::string& host_header) const;  // Host header from request

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
    void setDefaultServerBlock();

    // Parsing Helpers
    std::string getFileExtension(const std::string& filename) const;
    void parseLine(const std::string& line,
                   std::map<std::string, std::string>& configMap);
    bool skipEmptyAndCommentLines(std::istream& stream, std::string& line);
    bool parseServerBlock(std::istream& config_file);
    bool parseLocationBlock(std::istream& config_file, RouteConfig& route);
    std::string trimString(const std::string& str);
    void handleCgiExtDirective(std::map<std::string, std::string>& directives,
                               const std::string& line);
    void handleKeyOnlyDirective(std::map<std::string, std::string>& directives,
                                const std::string& trimmed_line,
                                size_t first_char);
    void handleKeyValueDirective(std::map<std::string, std::string>& directives,
                                 const std::string& trimmed_line,
                                 size_t first_char, size_t first_space);
    bool parseListenDirective(ServerBlock& server, const std::string& value);
    void parseServerNameDirective(ServerBlock& server,
                                  const std::string& value);
    bool parseMaxBodySizeDirective(ServerBlock& server,
                                   const std::string& value);
    bool parseErrorPageDirective(ServerBlock& server, const std::string& value);
    bool processServerDirective(ServerBlock& server, const std::string& key,
                                const std::string& value);
    bool validateLocationPath(const std::string& line, std::string& path);
    bool findLocationOpeningBrace(std::istream& config_file,
                                  const std::string& line, size_t& brace_pos);
    void parseRootDirective(RouteConfig& route, const std::string& value);
    void parseIndexDirective(RouteConfig& route, const std::string& value);
    void parseAutoindexDirective(RouteConfig& route, const std::string& value);
    bool parseReturnDirective(RouteConfig& route, const std::string& value);
    void parseMethodsDirective(RouteConfig& route, const std::string& value);
    void parseCgiDirective(RouteConfig& route, const std::string& key,
                           const std::string& value);
    void parseUploadDirective(RouteConfig& route, const std::string& key,
                              const std::string& value);
    bool processLocationDirective(RouteConfig& route, const std::string& key,
                                  const std::string& value);
    bool handleEmptyServerConfig(const std::string& filename);
    void updateConfigFromFirstServer();
    bool isRouteMatch(const RouteConfig& route, const std::string& path) const;
};

#endif  // SERVERCONFIG_HPP