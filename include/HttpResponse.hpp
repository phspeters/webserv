#ifndef HTTP_RESPONSE_HPP
#define HTTP_RESPONSE_HPP

#include "webserv.hpp"

enum ResponseStatus;

// Represents an HTTP Response to be sent back to the client.
// An instance of this is owned by Connection and pointed to by
// Connection::response_data_.
struct HttpResponse {
    //--------------------------------------
    // Response Data Members
    //--------------------------------------
    int status_code_;
    std::string status_message_;
    std::string
        version_;

    std::map<std::string, std::string> headers_;

    std::vector<char> body_;

    // Often useful to store these explicitly for header generation
    size_t content_length_;
    std::string content_type_;

    //--------------------------------------
    // Constructor / Destructor
    //--------------------------------------
    HttpResponse();
    ~HttpResponse();

    //--------------------------------------
    // Helper Methods
    //--------------------------------------
    void set_status(int code);
    void set_header(const std::string& name, const std::string& value);
    
    std::string get_header(const std::string& name) const;
    std::string get_headers_string() const;
    std::string get_status_line() const;

    void clear();

   private:
    // Prevent copying
    HttpResponse(const HttpResponse&);
    HttpResponse& operator=(const HttpResponse&);

};  // struct HttpResponse

enum ResponseStatus {
    // 2xx - Success
    OK = 200,          // Request succeeded
    CREATED = 201,     // Request succeeded and a new resource was created
    NO_CONTENT = 204,  // Request succeeded but returns no content

    // 3xx - Redirection
    MOVED_PERMANENTLY = 301,  // Resource permanently moved to a new URL
    FOUND = 302,              // Resource temporarily moved to a new URL
    NOT_MODIFIED = 304,  // Resource hasn't been modified since last request

    // 4xx - Client Errors
    BAD_REQUEST = 400,   // Server cannot process the request (syntax error)
    UNAUTHORIZED = 401,  // Authentication required
    FORBIDDEN = 403,     // Server understood but refuses to authorize
    NOT_FOUND = 404,     // Resource not found
    METHOD_NOT_ALLOWED = 405,  // Request method not supported
    REQUEST_TIMEOUT = 408,     // Server timed out waiting for request
    CONFLICT = 409,            // Request conflict with current state of server
    LENGTH_REQUIRED = 411,     // Content-Length required but not provided
    PAYLOAD_TOO_LARGE = 413,   // Request entity too large
    URI_TOO_LONG = 414,        //  Request URI too long
    UNSUPPORTED_MEDIA_TYPE = 415,  // Media format not supported
    HEADER_TOO_LONG = 431,         // Request header fields too large

    // 5xx - Server Errors
    INTERNAL_SERVER_ERROR = 500,  // Generic server error
    NOT_IMPLEMENTED = 501,        // Server does not support the functionality
    BAD_GATEWAY = 502,  // Server acting as gateway received invalid response
    SERVICE_UNAVAILABLE = 503,  // Server temporarily unavailable
    GATEWAY_TIMEOUT = 504,  //  Gateway server did not receive response in time
    HTTP_VERSION_NOT_SUPPORTED = 505,  // HTTP version in request not supported
    INSUFFICIENT_STORAGE = 507         // Insufficient Storage
};

#endif  // RESPONSE_HPP
