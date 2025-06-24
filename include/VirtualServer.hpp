#ifndef VIRTUALSERVER_HPP
#define VIRTUALSERVER_HPP

#include "webserv.hpp"

// Location configuration block
struct Location;

// Server configuration
struct VirtualServer {
    // Basic server properties
    std::string host_name_;
    std::string host_;
    int port_;
    bool listen_specified_;
    std::vector<std::string> server_names_;
    ssize_t client_max_body_size_;

    // Error pages mapping (status code -> file path)
    std::map<int, std::string> error_pages_;

    // Locations within this server
    std::vector<Location> locations_;

    // Default constructor
    VirtualServer();

    // Parse configuration from a file
    bool parse_server_block(std::ifstream& file);
    bool parse_location_block(std::ifstream& file, std::string line);
    bool handle_server_directive(const std::string& key,
                                 const std::string& value);
    bool parse_listen(const std::string& value);
    bool parse_server_name(const std::string& value);
    bool parse_error_page(const std::string& value);
    ssize_t parse_client_max_body_size(const std::string& value);
    bool parse_directive(const std::string& line, std::string& key,
                         std::string& value);
    bool add_directive_value(Location& location, const std::string& key,
                             const std::string& value);

    // Validation methods
    bool is_valid() const;
    bool is_valid_host() const;
    bool is_valid_port() const;
    bool has_valid_locations() const;
    bool has_valid_error_pages() const;
};

// Location configuration block
struct Location {
    std::string path_;
    std::string root_;
    bool autoindex_;
    std::vector<std::string> allowed_methods_;
    bool cgi_enabled_;
    std::string index_;
    std::string redirect_;
    ssize_t client_max_body_size_;

    // Constructor with defaults
    Location();

    // Validation method
    bool is_valid() const;
};

#endif  // VIRTUALSERVER_HPP