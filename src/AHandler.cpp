#include "webserv.hpp"

std::string AHandler::parse_absolute_path(HttpRequest* req) {

    // Extract request data
    const std::string& request_path = req->uri_;
    const LocationConfig* request_location = req->location_match_;
    std::string request_root = request_location->root;
    
    std::cout << "\n==== STATIC FILE HANDLER ====\n";
    std::cout << "Request URI: " << request_path << std::endl;
    std::cout << "Matched location: " << request_location->path << std::endl;
    std::cout << "Root: " << request_location->root << std::endl;

    // Calculate the path relative to the location
    std::string relative_path = "";
    
    // Calculate where the relative part starts
    size_t location_len = request_location->path.length();
    
    // If location path ends with /, exclude it from length calculation
    if (!request_location->path.empty() && request_location->path[location_len - 1] == '/') {
        location_len--;
    }
    
    // Extract the relative part (starting after the location path)
    if (request_path.length() > location_len) {
        relative_path = request_path.substr(location_len + 1);
        if(relative_path[0] != '/') {
            relative_path = "/" + relative_path;
        }
    }
   
    std::string absolute_path;
    
    if (!relative_path.empty() && relative_path[relative_path.size() - 1] == '/') {
        relative_path += "index.html"; // --TEMP --CHECK
    }
    absolute_path = request_root + relative_path;

    std::cout << "Relative path: " << relative_path << std::endl;
    std::cout << "Absolute path: " << absolute_path << std::endl;
    std::cout << "============================\n" << std::endl;

    return (absolute_path);
}

