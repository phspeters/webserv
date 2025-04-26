#ifndef ROUTECONFIG_HPP
#define ROUTECONFIG_HPP

#include "webserv.hpp"

class RouteConfig {
   public:
    std::string path_;
    std::string root_;
    std::string index_file_;
    bool directory_listing_;
    std::vector<std::string> allowed_methods_;
    std::string redirect_url_;
    int redirect_code_;          // 0 if no redirect
    std::string cgi_extension_;  // e.g., ".php"
    std::string cgi_handler_;    // e.g., "/usr/bin/php-cgi"
    bool uploads_enabled_;
    std::string upload_dir_;

    RouteConfig();  // Constructor with defaults
    bool is_method_allowed(const std::string& method) const;
};

#endif  // ROUTECONFIG_HPP