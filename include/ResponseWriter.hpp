#ifndef RESPONSEWRITER_HPP
#define RESPONSEWRITER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
struct HttpResponse;
struct VirtualServer;

// Utility class to help format HTTP responses.
class ResponseWriter {
   public:
    ResponseWriter();
    ~ResponseWriter();

    codes::WriterState write_response(Connection* conn);

    // Prepares the initial part of the response (status line + headers)
    // and places it into the Connection's write buffer.
    // Sets standard headers like Date, Server, Content-Length, Content-Type
    // based on Response object. Returns true on success, false on error.
    bool write_headers(Connection* conn);

    bool write_body(Connection* conn);

    // Generates a standard error response (e.g., 404 Not Found, 500 Internal
    // Server Error) directly into the connection's write buffer. Populates
    // conn->response_data with minimal info.
    void write_error_response(Connection* conn);

   private:
    std::string get_status_message(
        int code) const;  // Helper to get text for status code
    std::string get_current_gmt_time() const;  // Helper for Date header

    // Prevent copying
    ResponseWriter(const ResponseWriter&);
    ResponseWriter& operator=(const ResponseWriter&);

};  // class ResponseWriter

#endif  // RESPONSEWRITER_HPP