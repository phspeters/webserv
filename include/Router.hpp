#ifndef ROUTER_HPP
#define ROUTER_HPP

#include "webserv.hpp"

// Forward declarations
class AHandler;
struct ServerConfig;
class StaticFileHandler;
class CgiHandler;
class FileUploadHandler;

// Determines which Handler should process a given Request.
class Router {
   public:
    // Constructor takes references to the actual handler instances owned
    // by Server
    Router(const ServerConfig& config, StaticFileHandler* static_handler,
           CgiHandler* cgi_handler, FileUploadHandler* file_upload_handler);
    ~Router();

    // Selects the appropriate handler based on the request properties (URI,
    // method) and server configuration (CGI paths, etc.). Returns a pointer to
    // the handler instance responsible.
    AHandler* route(HttpRequest* req);

   private:
    const ServerConfig&
        config_;  // Reference to configuration for routing rules

    // Pointers to the handler instances (owned by Server, not Router)
    StaticFileHandler* static_handler_;
    CgiHandler* cgi_handler_;
    FileUploadHandler* file_upload_handler_;

    // Prevent copying
    Router(const Router&);
    Router& operator=(const Router&);

};  // class Router

#endif  // ROUTER_HPP
