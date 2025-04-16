#ifndef REQUEST_HPP
#define REQUEST_HPP

#include <map>
#include <string>
#include <vector>

// Represents a parsed HTTP Request.
// An instance of this is typically created during the parsing phase
// and pointed to by Connection::request_data.
class Request {
   public:
    //--------------------------------------
    // Request Data Members
    //--------------------------------------
    std::string method;   // e.g., "GET", "POST"
    std::string uri;      // The original, unmodified URI from the request line
    std::string version;  // e.g., "HTTP/1.1", "HTTP/1.0"

    std::map<std::string, std::string>
        headers;  // Map of header names to values

    std::vector<char> body;  // Request body content

    // Parsed components of the URI (populated after basic parsing)
    std::string path;          // Path part of the URI (e.g., "/index.html")
    std::string query_string;  // Query part of the URI (e.g., "a=1&b=2")

    bool parse_error;  // Flag indicating if a fatal parsing error occurred

    //--------------------------------------
    // Constructor / Destructor
    //--------------------------------------
    Request() : parse_error(false) {
        // Initialize strings, maps, vectors to empty states automatically
    }

    // Default destructor is likely sufficient as std containers manage their
    // own memory
    ~Request() {}

    //--------------------------------------
    // Helper Methods (optional declarations)
    //--------------------------------------
    // Example: Get a header value (case-insensitive lookup might be needed in
    // .cpp)
    std::string getHeader(const std::string& name) const;

    void clear() {
        method.clear();
        uri.clear();
        version.clear();
        headers.clear();
        body.clear();
        path.clear();
        query_string.clear();
        parse_error = false;
    }

   private:
    // Prevent copying if requests are managed by pointer in Connection
    Request(const Request&);
    Request& operator=(const Request&);

};  // class Request

#endif  // REQUEST_HPP
