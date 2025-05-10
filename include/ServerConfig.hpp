#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

// Location configuration block

// std::map<std::string, std::vector<std::string> > directives;
struct LocationConfig {
    std::string path;
    std::string root;
    bool autoindex;
    std::vector<std::string> allowed_methods;
    bool cgi_enabled;
    std::string index;
    std::string redirect;

    // Constructor with defaults
    LocationConfig();

    // Validation method
    bool isValid(std::string& error_msg) const;
};

// Server configuration
struct ServerConfig {
    // Basic server properties
    std::string host_;
    int port_;
    bool listen_specified_;
    std::vector<std::string> server_names_;
    size_t client_max_body_size_;

    // Error pages mapping (status code -> file path)
    std::map<int, std::string> error_pages;

    // Locations within this server
    std::vector<LocationConfig> locations;

    // Default constructor
    ServerConfig();

    // Parse configuration from a file
    static bool parseServerBlock(std::ifstream& file, ServerConfig& config);
    static bool parseLocationBlock(std::ifstream& file, std::string line,
                                   ServerConfig& config);
    static bool handleServerDirective(const std::string& key,
                                      const std::string& value,
                                      ServerConfig& config);
    static bool parseListen(const std::string& value, ServerConfig& config);
    static bool parseServerName(const std::string& value, ServerConfig& config);
    static bool parseErrorPage(const std::string& value, ServerConfig& config);
    static bool parseClientMaxBodySize(const std::string& value,
                                       ServerConfig& config);
    static bool parseDirective(const std::string& line, std::string& key,
                               std::string& value);
    static bool addDirectiveValue(LocationConfig& location,
                                  const std::string& key,
                                  const std::string& value);

    bool applyDefaults();

    // Validation methods
    bool isValid(std::string& error_msg) const;
    bool isValidHost(std::string& error_msg) const;
    bool isValidPort(std::string& error_msg) const;
    bool hasValidLocations(std::string& error_msg) const;

    // Print configuration (for debugging)
    void print() const;
};

#endif  // SERVERCONFIG_HPP