#ifndef VIRTUALSERVER_HPP
#define VIRTUALSERVER_HPP

#include "webserv.hpp"

// Location configuration block
struct Location {
    std::string path_;
    std::string root_;
    bool autoindex_;
    std::vector<std::string> allowed_methods_;
    bool cgi_enabled_;
    std::string index_;
    std::string redirect_;

    // Constructor with defaults
    Location();

    // Validation method
    bool is_valid(std::string& error_msg) const;
};

// Server configuration
struct VirtualServer {
    // Basic server properties
    std::string host_name_;
    std::string host_;
    int port_;
    bool listen_specified_;
    std::vector<std::string> server_names_;
    size_t client_max_body_size_;

    // Error pages mapping (status code -> file path)
    std::map<int, std::string> error_pages_;

    // Locations within this server
    std::vector<Location> locations_;

    // Default constructor
    VirtualServer();

    // Parse configuration from a file
    static bool parse_server_block(std::ifstream& file, VirtualServer& config);
    static bool parse_location_block(std::ifstream& file, std::string line,
                                     VirtualServer& config);
    static bool handle_server_directive(const std::string& key,
                                        const std::string& value,
                                        VirtualServer& config);
    static bool parse_listen(const std::string& value, VirtualServer& config);
    static bool parse_server_name(const std::string& value,
                                  VirtualServer& config);
    static bool parse_error_page(const std::string& value,
                                 VirtualServer& config);
    static bool parse_client_max_body_size(const std::string& value,
                                           VirtualServer& config);
    static bool parse_directive(const std::string& line, std::string& key,
                                std::string& value);
    static bool add_directive_value(Location& location, const std::string& key,
                                    const std::string& value);
    bool apply_defaults();

    // Validation methods
    bool is_valid(std::string& error_msg) const;
    bool is_valid_host(std::string& error_msg) const;
    bool is_valid_port(std::string& error_msg) const;
    bool has_valid_locations(std::string& error_msg) const;
};

#endif  // VIRTUALSERVER_HPP