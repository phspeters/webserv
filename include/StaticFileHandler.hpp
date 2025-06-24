#ifndef STATICFILEHANDLER_HPP
#define STATICFILEHANDLER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
struct VirtualServer;
struct StaticFileContext;

// Handles requests for static files.
class StaticFileHandler : public AHandler {
   public:
    // Constructor takes dependencies
    StaticFileHandler();
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
    // Helper methods for path resolution, MIME type lookup etc. go in .cpp
    // bool process_directory_redirect(Connection* conn,
    //                                 std::string& absolute_path);
    // bool process_directory_index(Connection* conn, std::string&
    // absolute_path,
    //                              bool& need_autoindex);
    // void generate_directory_listing(Connection* conn,
    //                                 const std::string& dir_path);

    // Prevent copying
    StaticFileHandler(const StaticFileHandler&);
    StaticFileHandler& operator=(const StaticFileHandler&);
};  // class StaticFileHandler

struct StaticFileContext {
    // State for static file handling
    int file_fd_;   // File descriptor for the opened static file (-1 if none)
    off_t offset_;  // Offset in the file for sending data
    size_t bytes_to_send_;  // Total bytes to send from the file
    size_t bytes_sent_;     // Bytes already sent from the file

    StaticFileContext()
        : file_fd_(-1), offset_(0), bytes_to_send_(0), bytes_sent_(0) {}
};

#endif  // STATICFILEHANDLER_HPP