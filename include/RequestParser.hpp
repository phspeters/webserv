#ifndef REQUESTPARSER_HPP
#define REQUESTPARSER_HPP

// Forward declarations
class Connection;
struct ServerConfig;

// Parses HTTP requests incrementally from a Connection's read buffer.
class RequestParser {
   public:
    // Result of a parsing attempt
    enum ParseResult {
        PARSE_OK,               // Request fully parsed successfully
        PARSE_NEED_MORE,        // Need more data from the client
        PARSE_ERROR_MALFORMED,  // Syntax error in request
        PARSE_ERROR_TOO_LARGE,  // Request exceeds limits (e.g., body size)
        PARSE_ERROR_INTERNAL    // Other internal errors
    };

    explicit RequestParser(
        const ServerConfig& config);  // Takes config for limits etc.
    ~RequestParser();

    // Parses data currently in the connection's read buffer.
    // - Allocates/populates conn->request_data if successful (PARSE_OK).
    // - Updates conn->read_buffer (removing parsed data).
    // - Returns the result status.
    ParseResult parse(Connection* conn);

   private:
    const ServerConfig& config;  // Reference to server configuration

    // Internal helper methods for different parsing states would go in .cpp
    // e.g., parseRequestLine, parseHeaders, parseBody

    // Prevent copying
    RequestParser(const RequestParser&);
    RequestParser& operator=(const RequestParser&);

};  // class RequestParser

#endif  // REQUESTPARSER_HPP
