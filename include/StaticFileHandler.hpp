#ifndef STATICFILEHANDLER_HPP
#define STATICFILEHANDLER_HPP

#include "common.hpp"

struct Connection;
struct StaticFileContext;

class StaticFileHandler : public AHandler {
   public:
    StaticFileHandler();
    virtual ~StaticFileHandler();

    // Implementation of the handle method for static files.
    // - Validates request method (GET, HEAD).
    // - Resolves file path based on server root and request URI.
    // - Checks file existence and permissions.
    // - Opens file, prepares response headers (status, content-type, length).
    // - Sets up Connection state for sending (file FD, offset, bytes).
    // - Uses ResponseWriter to format headers into Connection write buffer.
    virtual Result check_permissions(Connection* conn);
    virtual Result setup_handler(Connection* conn);
    virtual Result handle_event(Connection* conn);
    virtual void cleanup_handler(Connection* conn);
    virtual bool is_asynchronous() const { return true; }

   private:
    // Prevent copying
    StaticFileHandler(const StaticFileHandler&);
    StaticFileHandler& operator=(const StaticFileHandler&);

};  // class StaticFileHandler

struct StaticFileContext {
    int file_fd_;
    off_t offset_;
    size_t bytes_to_send_;
    size_t bytes_sent_;
    std::string absolute_path_;

    StaticFileContext()
        : file_fd_(-1), offset_(0), bytes_to_send_(0), bytes_sent_(0) {}
};

#endif  // STATICFILEHANDLER_HPP