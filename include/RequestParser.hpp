#ifndef REQUESTPARSER_HPP
#define REQUESTPARSER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
struct HttpRequest;
struct ServerConfig;

#define CRLF "\r\n"  // Carriage return + line feed

// In RequestParser.hpp or a separate config.hpp
namespace HttpConfig {
const size_t MAX_METHOD_LENGTH = 8;
const size_t MAX_REQUEST_LINE_LENGTH = 8192;  // 8KB
const size_t MAX_PATH_LENGTH = 2048;      // Path component
const size_t MAX_QUERY_LENGTH = 2048;     // Query string
const size_t MAX_FRAGMENT_LENGTH = 1024;  // Fragment identifier
const size_t MAX_HEADER_NAME_LENGTH = 256;
const size_t MAX_HEADER_VALUE_LENGTH = 8192;
const size_t MAX_HEADERS = 100;
const size_t MAX_CONTENT_LENGTH = 10485760;  // 10MB
// temp
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
        PARSE_INVALID_REQUEST_LINE,    // Invalid request line
		PARSE_METHOD_NOT_ALLOWED,      // Unsupported HTTP method
        PARSE_INVALID_PATH,            // Invalid path in URI
		PARSE_INVALID_QUERY_STRING,    // Invalid query string in URI
		PARSE_INVALID_FRAGMENT,        // Invalid fragment in URI
		PARSE_VERSION_NOT_SUPPORTED,   // Unsupported HTTP version
        PARSE_REQUEST_TOO_LONG,        // Request exceeds maximum length
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
    // - Allocates/populates conn->request_data if successful (PARSE_SUCCESS).
    // - Updates conn->read_buffer (removing parsed data).
    // - Returns the result status.
    ParseResult parse(Connection* conn);

   private:
    ParserState current_state_;   // Current state of the parser
    const ServerConfig& config_;  // Reference to server configuration

    // Request line parsing methods
    ParseResult parse_request_line(Connection* conn);
    bool split_request_line(HttpRequest* request, std::string& request_line);
    bool split_uri_components(HttpRequest* request);
    std::string decode_uri(const std::string& uri);
    ParseResult validate_request_line(const HttpRequest* request);
    bool validate_method(const std::string& method);
    bool validate_path(const std::string& path);
    bool validate_query_string(const std::string& query_string);
    bool validate_fragment(const std::string& fragment);
    bool validate_http_version(const std::string& version);

    // Header parsing methods
    ParseResult parse_headers(Connection* conn);
    ParseResult process_single_header(const std::string& header_line,
                                      HttpRequest* request);
    ParseResult determine_request_body_handling(HttpRequest* request);

    // Body parsing methods
    ParseResult parse_body(Connection* conn);
    ParseResult parse_chunked_body(Connection* conn);
    ParseResult parse_chunk_header(std::vector<char>& buffer,
                                   size_t& out_chunk_size);
    ParseResult read_chunk_data(std::vector<char>& buffer, HttpRequest* request,
                                size_t& chunk_remaining_bytes);
    ParseResult process_chunk_terminator(std::vector<char>& buffer);
    ParseResult finish_chunked_parsing(std::vector<char>& buffer);
    size_t chunk_remaining_bytes_;

    void reset_parser_state();

    // Prevent copying
    RequestParser(const RequestParser&);
    RequestParser& operator=(const RequestParser&);

};  // class RequestParser

#endif  // REQUESTPARSER_HPP
