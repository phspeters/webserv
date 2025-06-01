#ifndef HTTP_RESPONSE_HPP
#define HTTP_RESPONSE_HPP

#include "webserv.hpp"

// Represents an HTTP Response to be sent back to the client.
// An instance of this is owned by Connection and pointed to by
// Connection::response_data_.
struct HttpResponse {
    //--------------------------------------
    // Response Data Members
    //--------------------------------------
    int status_code_;             // e.g., 200, 404
    std::string status_message_;  // e.g., "OK", "Not Found"
    std::string
        version_;  // e.g., "HTTP/1.1" (usually match request or default)

    std::map<std::string, std::string> headers_;  // Response headers

    std::vector<char> body_;  // Response body content

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
    std::string get_header(const std::string& name) const;

    // Method to generate the full status line + headers string
    std::string get_headers_string() const;

    // Method to generate the status line only
    std::string get_status_line() const;

    void clear();

   private:
    // Prevent copying if responses are managed by pointer in Connection
    HttpResponse(const HttpResponse&);
    HttpResponse& operator=(const HttpResponse&);

};  // class Response

#endif  // RESPONSE_HPP
