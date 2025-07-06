#ifndef STATICFILEHANDLER_HPP
#define STATICFILEHANDLER_HPP

#include "common.hpp"

struct Connection;
struct StaticFileContext;

class StaticFileHandler : public AHandler {
   public:
    StaticFileHandler();
    virtual ~StaticFileHandler();

    virtual Result handle(Connection* conn);
    virtual void cleanup_handler(Connection* conn);

    bool is_asynchronous() const { return false; }

   private:
    Result validate_method(Connection* conn);
    Result handle_location_redirect(Connection* conn);
    Result resolve_absolute_path(Connection* conn, std::string& absolute_path);
    Result handle_directory_request(Connection* conn,
                                    std::string& absolute_path);
    Result validate_file_access(Connection* conn,
                                const std::string& absolute_path);
    Result prepare_file_response(Connection* conn,
                                 const std::string& absolute_path);
    Result handle_file_open_error(Connection* conn);
    void set_response_headers(Connection* conn, const struct stat& file_info);
    std::string determine_content_type(const std::string& path);

    // Prevent copying
    StaticFileHandler(const StaticFileHandler&);
    StaticFileHandler& operator=(const StaticFileHandler&);

};  // class StaticFileHandler

struct StaticFileContext {
    int file_fd_;
    std::string absolute_path_;

    StaticFileContext() : file_fd_(-1) {}
};

#endif  // STATICFILEHANDLER_HPP