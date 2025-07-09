#ifndef AHANDLER_HPP
#define AHANDLER_HPP

#include "common.hpp"

struct Connection;
struct HttpRequest;

// Abstract base class for all request handlers.
class AHandler {
   public:
    virtual ~AHandler() {}

    virtual Result initialize_context(Connection* conn) = 0;
    virtual ParseStatus check_permissions(Connection* conn) = 0;
    virtual Result setup_handler(Connection* conn) = 0;
    virtual Result handle(Connection* conn) = 0;
    virtual void cleanup_handler(Connection* conn) = 0;

    virtual bool is_asynchronous() const = 0;

   protected:
    std::string parse_absolute_path(Connection* conn);
};  // class AHandler

#endif  // AHANDLER_HPP