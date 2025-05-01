#ifndef REQUESTPARSER_HPP
#define REQUESTPARSER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
class HttpRequest;
struct ServerConfig;

#define CRLF "\r\n"  // Carriage return + line feed

// In RequestParser.hpp or a separate config.hpp
namespace HttpConfig {
const size_t MAX_METHOD_LENGTH = 8;
const size_t MAX_URI_LENGTH = 8192;
const size_t MAX_HEADER_NAME_LENGTH = 256;
const size_t MAX_HEADER_VALUE_LENGTH = 8192;
const size_t MAX_HEADERS = 100;
const size_t MAX_CONTENT_LENGTH = 10485760;  // 10MB
// temp
const size_t MAX_REQUEST_LINE_LENGTH = 8192;  // 8KB
const size_t MAX_BODY_SIZE = 10485760;        // 10MB
const size_t MAX_CHUNK_SIZE = 1048576;        // 1MB
const char SPACE = ' ';                       // Space character
const char COLON = ':';                       // Colon character
const char SEMICOLON = ';';                   // Semicolon character
}  // namespace HttpConfig

// Parses HTTP requests incrementally from a Connection's read buffer.
class RequestParser {
   public:
    // Result of a parsing attempt
    enum ParseResult {
        PARSE_SUCCESS,                 // Request fully parsed
        PARSE_INCOMPLETE,              // Need more data
        PARSE_ERROR,                   // General parsing error
        PARSE_URI_TOO_LONG,            // URI exceeds maximum length
        PARSE_HEADER_TOO_LONG,         // Header exceeds maximum length
        PARSE_TOO_MANY_HEADERS,        // Too many headers
        PARSE_INVALID_CONTENT_LENGTH,  // Content-Length header is invalid
        PARSE_CONTENT_TOO_LARGE,       // Content length exceeds maximum
        PARSE_INVALID_CHUNK_SIZE       // Invalid chunk size in chunked encoding
    };

    // Represents the current state of the parser
    enum ParserState {
        PARSING_REQUEST_LINE,  // Combines method, URI, HTTP version
        PARSING_HEADERS,       // All header parsing
        PARSING_BODY,          // Normal body
        PARSING_CHUNKED_BODY,  // Chunked transfer encoding
        PARSING_COMPLETE       // Request fully parsed
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
    ParserState currentState_;  // Current state of the parser
    // Reference to server configuration for limits
    const ServerConfig& config_;  // Reference to server configuration

    // Internal helper methods for different parsing states would go in .cpp
    ParseResult parse_request_line(Connection* conn);
    ParseResult parse_headers(Connection* conn);
    ParseResult parse_body(Connection* conn);
    ParseResult parse_chunked_body(Connection* conn);

    size_t
        chunkRemaining_;  // Remaining bytes in the current chunk (if chunked)

    // Prevent copying
    RequestParser(const RequestParser&);
    RequestParser& operator=(const RequestParser&);

};  // class RequestParser

#endif  // REQUESTPARSER_HPP
