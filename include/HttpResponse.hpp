#ifndef HTTP_RESPONSE_HPP
#define HTTP_RESPONSE_HPP

#include "common.hpp"

// Represents an HTTP Response to be sent back to the client.
// An instance of this is owned by Connection and pointed to by
// Connection::response_data_.
struct HttpResponse {
    //--------------------------------------
    // Response Data Members
    //--------------------------------------
    int status_code_;
    std::string status_message_;
    std::string version_;

    std::map<std::string, std::string> headers_;

    std::vector<char> body_;
    int body_fd_;  // File descriptor for file-based responses (e.g., static
                   // files, cgi scripts)

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

#endif  // RESPONSE_HPP
