#ifndef AHANDLER_HPP
#define AHANDLER_HPP

#include "common.hpp"

struct Connection;
struct HttpRequest;

// Abstract base class for all request handlers.
class AHandler {
   public:
    virtual ~AHandler() {}

    virtual ResponseStatus check_permissions(Connection* conn) = 0;
    virtual ResponseStatus setup_handler(Connection* conn) = 0;
    virtual ResponseStatus handle_event(Connection* conn) = 0;
    virtual void cleanup_handler(Connection* conn) = 0;

   protected:
    std::string parse_absolute_path(Connection* conn);
    ResponseStatus process_location_redirect(Connection* conn);

    ResponseStatus process_directory_redirect(Connection* conn,
                                    std::string& absolute_path);
    ResponseStatus process_directory_index(Connection* conn, std::string& absolute_path,
                                 bool& need_autoindex);
    ResponseStatus generate_directory_listing(Connection* conn,
                                    const std::string& dir_path);

};  // class Handler

#endif  // HANDLER_HPP