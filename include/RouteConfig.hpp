#ifndef ROUTECONFIG_HPP
#define ROUTECONFIG_HPP

#include <string>
#include <vector>

class RouteConfig {
   public:
    std::string path;
    std::string root;
    std::string index_file;
    bool directory_listing;
    std::vector<std::string> allowed_methods;
    std::string redirect_url;
    int redirect_code;          // 0 if no redirect
    std::string cgi_extension;  // e.g., ".php"
    std::string cgi_handler;    // e.g., "/usr/bin/php-cgi"
    bool uploads_enabled;
    std::string upload_dir;

    RouteConfig();  // Constructor with defaults
    bool isMethodAllowed(const std::string& method) const;
};

#endif  // ROUTECONFIG_HPP