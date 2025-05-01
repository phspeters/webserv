#ifndef ROUTER_HPP
#define ROUTER_HPP

#include "webserv.hpp"

// Forward declarations
// class Request;
// class Handler;
// struct ServerConfig;
// class StaticFileHandler;  // Need specific handlers if router holds pointers
// class CgiHandler;

// Determines which Handler should process a given Request.
class Router {
   public:
    // Constructor might take references to the actual handler instances owned
    // by Server
    Router(const ServerConfig& config);
    ~Router();

    // Selects the appropriate handler based on the request properties (URI,
    // method) and server configuration (CGI paths, etc.). Returns a pointer to
    // the handler instance responsible.
    IHandler* route(const HttpRequest* req);

   private:
    const ServerConfig&
        config_;  // Reference to configuration for routing rules
    const IHandler* handler_;

    // Pointers to the handler instances (owned by Server, not Router)
    // StaticFileHandler* static_handler_;
    // CgiHandler* cgi_handler_;
    // Add pointers for other handlers...

    // Prevent copying
    Router(const Router&);
    Router& operator=(const Router&);

};  // class Router

#endif  // ROUTER_HPP
