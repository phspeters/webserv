#ifndef SERVERBLOCK_HPP
#define SERVERBLOCK_HPP

#include "webserv.hpp"

class ServerBlock {
   public:
    std::string host_;
    unsigned short port_;
    std::vector<std::string> server_names_;
    size_t max_body_size_;
    std::map<int, std::string>
        error_pages_;  // Map status code to error page path
    std::vector<RouteConfig> routes_;

    ServerBlock();  // Constructor with defaults
    std::string get_error_page(int status_code) const;
};

#endif  // SERVERBLOCK_HPP