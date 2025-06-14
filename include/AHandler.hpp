#ifndef AHANDLER_HPP
#define AHANDLER_HPP

#include "webserv.hpp"

// Forward declaration
struct Connection;
struct HttpRequest;

// Abstract base class for all request handlers.
class AHandler {
   public:
    // Virtual destructor is essential for classes intended for polymorphic
    // deletion
    virtual ~AHandler() {}

    // Main method to handle a request associated with a connection.
    // The handler is responsible for:
    // 1. Processing the request (conn->request_data).
    // 2. Generating a response (populating conn->response_data).
    // 3. Preparing the connection for writing (populating conn->write_buffer
    // with headers,
    //    setting up file FDs if needed, potentially requesting EPOLLOUT).
    // Should handle errors and potentially set conn->state = CONN_ERROR.
    virtual void handle(Connection* conn) = 0;
    // Optional: Methods for handling specific I/O events if needed,
    // allowing the Server's main loop to delegate more directly.
    // virtual void on_readable(Connection* conn) {} // e.g., For CGI reading
    // stdout virtual void on_writable(Connection* conn) {} // e.g., For CGI
    // writing stdin or sending file chunks
   protected:
    std::string parse_absolute_path(Connection* conn);
    bool process_location_redirect(Connection* conn);

    bool process_directory_redirect(Connection* conn,
                                    std::string& absolute_path);
    bool process_directory_index(Connection* conn, std::string& absolute_path,
                                 bool& need_autoindex);
    void generate_directory_listing(Connection* conn,
                                    const std::string& dir_path);

};  // class Handler

#endif  // HANDLER_HPP