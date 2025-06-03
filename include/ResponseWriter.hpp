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

    codes::WriteStatus write_response(Connection* conn);

    // Prepares the initial part of the response (status line + headers)
    // and places it into the Connection's write buffer.
    // Sets standard headers like Date, Server, Content-Length, Content-Type
    // based on Response object. Returns true on success, false on error.
    bool write_headers(Connection* conn);

    bool write_body(Connection* conn);

   private:
    std::string get_current_gmt_time() const;  // Helper for Date header

    // Prevent copying
    ResponseWriter(const ResponseWriter&);
    ResponseWriter& operator=(const ResponseWriter&);

};  // class ResponseWriter

#endif  // RESPONSEWRITER_HPP