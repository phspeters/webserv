#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP

#include "webserv.hpp"

struct Location;

// Represents a parsed HTTP Request.
// An instance of this is typically created during the parsing phase
// and pointed to by Connection::request_data.
struct HttpRequest {
    //--------------------------------------
    // Request Data Members
    //--------------------------------------
    std::string method_;   // e.g., "GET", "POST"
    std::string uri_;      // The original, unmodified URI from the request line
    std::string version_;  // e.g., "HTTP/1.1", "HTTP/1.0"

    std::map<std::string, std::string>
        headers_;  // Map of header names to values

    std::vector<char> body_;  // Request body content

    // Parsed components of the URI (populated after basic parsing)
    std::string path_;          // Path part of the URI (e.g., "/index.html")
    std::string query_string_;  // Query part of the UR  I (e.g., "a=1&b=2")

    //--------------------------------------
    // Constructor / Destructor
    //--------------------------------------
    HttpRequest();
    ~HttpRequest();

    //--------------------------------------
    // Helper Methods
    //--------------------------------------
    std::string get_header(const std::string& name) const;
    void set_header(const std::string& name, const std::string& value);
    void clear();

   private:
    // Prevent copying
    HttpRequest(const HttpRequest&);
    HttpRequest& operator=(const HttpRequest&);

};  // class Request

#endif  // REQUEST_HPP
