#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

// Define allowed location directives as an enum
enum LocationDirective {
    ROOT,
    AUTOINDEX,
    ALLOWED_METHODS,
    CGI,
    INDEX,
    REDIRECT,
    // CGI_EXT,
    UNKNOWN
};

// Location configuration block
struct LocationConfig {
    std::string path;
    std::map<std::string, std::vector<std::string> > directives;

    // Static mapping between directive names and enum values
    static std::map<std::string, LocationDirective> directive_map;

    // Initialize the directive map (called once at startup)
    static void initDirectiveMap();

    // Convert string to directive enum
    static LocationDirective getDirectiveType(const std::string& directive);

    // Validation methods
    bool isValid(std::string& error_msg) const;
    static bool hasSingleValue(
        const std::map<std::string, std::vector<std::string> >& directives,
        const std::string& key, std::string& error_msg);
    void applyDefaults();
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

    // Other directives
    std::map<std::string, std::vector<std::string> > directives;

    // Default constructor
    ServerConfig();

    static bool parseServerBlock(std::ifstream& file, ServerConfig& config);
    static bool parseDirective(const std::string& line, std::string& key,
                               std::string& value);
    static void addDirectiveValue(
        std::map<std::string, std::vector<std::string> >& directives,
        const std::string& key, const std::string& value);

    bool applyDefaults();

    // Add validation method
    bool isValid(std::string& error_msg) const;

    // Helper methods for validation
    bool isValidHost(std::string& error_msg) const;
    bool isValidPort(std::string& error_msg) const;
    bool hasValidLocations(std::string& error_msg) const;

    // Print configuration (for debugging)
    void print() const;
};

#endif  // SERVERCONFIG_HPP