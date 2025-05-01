#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

// Location configuration block

// std::map<std::string, std::vector<std::string> > directives;
struct LocationConfig {
    std::string path;
    std::map<std::string, std::string> directives;
};

// Server configuration
struct ServerConfig {
    // Basic server properties
    std::string host_;
    int port_;
    std::vector<std::string> server_names_;
    size_t client_max_body_size_;

    // Error pages mapping (status code file path)
    std::map<int, std::string> error_pages;

    // Locations within this server
    std::vector<LocationConfig> locations;

    // Other directives
    std::map<std::string, std::string> directives;

    // Default constructor
    ServerConfig();

    static bool parseServerBlock(std::ifstream& file, ServerConfig& config);
    static bool parseDirective(const std::string& line, std::string& key,
                               std::string& value);

    // Print configuration (for debugging)
    void print() const;
};

#endif  // SERVERCONFIG_HPP