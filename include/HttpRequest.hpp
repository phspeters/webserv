#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP

#include "common.hpp"

struct Location;

// Represents a parsed HTTP Request.
// An instance of this is typically created during the parsing phase
// and pointed to by Connection::request_data.
struct HttpRequest {
    //--------------------------------------
    // Request Data Members
    //--------------------------------------
    std::string method_;
    std::string uri_;
    std::string version_;

    std::map<std::string, std::string> headers_;

    Buffer body_buffer_;   // Reserved to DEFAUL_CHUNK_SIZE
    bool body_fully_parsed_;

    size_t content_length_;
    // std::string content_type_; // TODO: Debate if we need this

    std::string path_;
    std::string query_string_;

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
