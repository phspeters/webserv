#include "webserv.hpp"

Router::Router(const ServerConfig& config)
    : config_(config) {}


Router::~Router() {
    // Clean up owned components
}

IHandler* Router::route(const HttpRequest* req) {
    
    // GET / HTTP/1.1
    // Host: localhost
    // Connection: keep-alive

    // Extract request information 
    // req->method_;
    // req->uri_;
    // req->version_;
    // req->headers_;
    // req->body_;


    // listen 

    
    // // Extract method from the server configuration location
    // methods = config_.get_methods();
    // if(is_allowed_method(req->method_, methods) == false)
    //     return nullptr;  // Method not allowed

    // // Check if the location uri matches the request uri



    // return nullptr;  // No suitable handler found
}

