#ifndef STATICFILEHANDLER_HPP
#define STATICFILEHANDLER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
struct ServerConfig;

// Handles requests for static files.
class StaticFileHandler : public AHandler {
   public:
    // Constructor takes dependencies
    StaticFileHandler(const ServerConfig& config);
    virtual ~StaticFileHandler();

    // Implementation of the handle method for static files.
    // - Validates request method (GET, HEAD).
    // - Resolves file path based on server root and request URI.
    // - Checks file existence and permissions.
    // - Opens file, prepares response headers (status, content-type, length).
    // - Sets up Connection state for sending (file FD, offset, bytes).
    // - Uses ResponseWriter to format headers into Connection write buffer.
    virtual void handle(Connection* conn);

    // Optional: Could override on_writable if complex chunked sending needed,
    // but often the main Server write loop can handle simple file sending.

   private:
    const ServerConfig& config_;  // Reference to server configuration

    // Helper methods for path resolution, MIME type lookup etc. go in .cpp

    // Prevent copying
    StaticFileHandler(const StaticFileHandler&);
    StaticFileHandler& operator=(const StaticFileHandler&);
    std::string parse_absolute_path(HttpRequest* req);

};  // class StaticFileHandler

#endif  // STATICFILEHANDLER_HPP
