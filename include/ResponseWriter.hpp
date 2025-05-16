#ifndef RESPONSEWRITER_HPP
#define RESPONSEWRITER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
struct HttpResponse;
struct ServerConfig;

// Utility class to help format HTTP responses.
class ResponseWriter {
   public:
    enum ResponseStatus {
        RESPONSE_COMPLETE,     // Response fully sent
        RESPONSE_INCOMPLETE,   // Partial write, needs another EPOLLOUT event
        RESPONSE_ERROR         // Error occurred during writing
    };

    explicit ResponseWriter(const ServerConfig& config);
    ~ResponseWriter();

    ResponseStatus write_response(Connection* conn);

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
    const ServerConfig&
        config_;  // Reference to config (e.g., for Server header)

    std::string get_status_message(
        int code) const;  // Helper to get text for status code
    std::string get_current_gmt_time() const;  // Helper for Date header

    // Prevent copying
    ResponseWriter(const ResponseWriter&);
    ResponseWriter& operator=(const ResponseWriter&);

};  // class ResponseWriter

#endif  // RESPONSEWRITER_HPP