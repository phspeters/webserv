#ifndef SERVERBLOCK_HPP
#define SERVERBLOCK_HPP

#include <map>
#include <string>
#include <vector>

#include "RouteConfig.hpp"

class ServerBlock {
   public:
    std::string host;
    unsigned short port;
    std::vector<std::string> server_names;
    size_t max_body_size;
    std::map<int, std::string>
        error_pages;  // Map status code to error page path
    std::vector<RouteConfig> routes;

    ServerBlock();  // Constructor with defaults
    std::string getErrorPage(int status_code) const;
};

#endif  // SERVERBLOCK_HPP