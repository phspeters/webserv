#ifndef AHANDLER_HPP
#define AHANDLER_HPP

#include "common.hpp"

struct Connection;
struct HttpRequest;

// Abstract base class for all request handlers.
class AHandler {
   public:
    virtual ~AHandler() {}

    virtual Result handle(Connection* conn) = 0;
    virtual void cleanup_handler(Connection* conn) = 0;

    virtual bool is_asynchronous() const = 0;

   protected:
    std::string parse_absolute_path(Connection* conn);
    bool process_location_redirect(Connection* conn);

    bool process_directory_redirect(Connection* conn,
                                    std::string& absolute_path);
    bool process_directory_index(Connection* conn, std::string& absolute_path,
                                 bool& need_autoindex);
    bool generate_directory_listing(Connection* conn,
                                    const std::string& dir_path);

};  // class Handler

#endif  // HANDLER_HPP