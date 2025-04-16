#ifndef ROUTER_HPP
#define ROUTER_HPP

#include <map>  // For potentially storing handler pointers

// Forward declarations
class Request;
class Handler;
struct ServerConfig;
class StaticFileHandler;  // Need specific handlers if router holds pointers
class CgiHandler;
// Add other specific handler types here

// Determines which Handler should process a given Request.
class Router {
   public:
    // Constructor might take references to the actual handler instances owned
    // by Server
    Router(
        const ServerConfig& config,
        StaticFileHandler*
            static_handler,  // Pointer to the single static handler instance
        CgiHandler* cgi_handler  // Pointer to the single CGI handler instance
        /* Add other handlers */);
    ~Router();

    // Selects the appropriate handler based on the request properties (URI,
    // method) and server configuration (CGI paths, etc.). Returns a pointer to
    // the handler instance responsible.
    Handler* route(const Request* req);

   private:
    const ServerConfig& config;  // Reference to configuration for routing rules

    // Pointers to the handler instances (owned by Server, not Router)
    StaticFileHandler* static_handler;
    CgiHandler* cgi_handler;
    // Add pointers for other handlers...

    // Prevent copying
    Router(const Router&);
    Router& operator=(const Router&);

};  // class Router

#endif  // ROUTER_HPP
