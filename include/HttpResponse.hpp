#ifndef HTTP_RESPONSE_HPP
#define HTTP_RESPONSE_HPP

#include "webserv.hpp"

// Represents an HTTP Response to be sent back to the client.
// An instance of this is typically created by a handler
// and pointed to by Connection::response_data.
struct HttpResponse {
    //--------------------------------------
    // Response Data Members
    //--------------------------------------
    int status_code_;             // e.g., 200, 404
    std::string status_message_;  // e.g., "OK", "Not Found"
    std::string
        version_;  // e.g., "HTTP/1.1" (usually match request or default)

    std::map<std::string, std::string> headers_;  // Response headers

    std::vector<char> body_;  // Response body content (if generated in memory)

    // State flags (might be managed in Connection instead, depending on design)
    // bool        headers_built; // Indicate if headers string is ready
    // bool        headers_sent;
    // bool        body_sent;

    // Often useful to store these explicitly for header generation
    size_t content_length_;
    std::string content_type_;

    //--------------------------------------
    // Constructor / Destructor
    //--------------------------------------
    HttpResponse();
    ~HttpResponse();

    //--------------------------------------
    // Helper Methods (declarations)
    //--------------------------------------
    void set_header(const std::string& name, const std::string& value);
    void set_status(int code);
    static std::string get_status_message(int code);

    // Method to generate the full status line + headers string (implementation
    // in .cpp)
    std::string get_headers_string() const;

    // Method to generate the status line only (implementation in .cpp)
    std::string get_status_line() const;

    void clear();

   private:
    // Prevent copying if responses are managed by pointer in Connection
    HttpResponse(const HttpResponse&);
    HttpResponse& operator=(const HttpResponse&);

};  // class Response

#endif  // RESPONSE_HPP
