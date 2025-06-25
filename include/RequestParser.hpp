#ifndef REQUESTPARSER_HPP
#define REQUESTPARSER_HPP

#include "webserv.hpp"

struct Connection;
struct HttpRequest;
struct VirtualServer;
enum ParserState;
enum ParseStatus;
struct ParserContext;

// Parses HTTP requests incrementally from a Connection's read buffer.
class RequestParser {
   public:
    RequestParser();
    ~RequestParser();

    ParseStatus parse_request_line(Connection* conn);
    ParseStatus parse_headers(Connection* conn);
    ParseStatus process_request(Connection* conn);
    ConnectionState determine_body_handling_state(Connection* conn);
    ParseStatus parse_body(Connection* conn);
    ParseStatus parse_chunked_body(Connection* conn);
    ParseStatus parse_multipart_body(Connection* conn);

   private:
    ParseStatus commit_request_line(HttpRequest* request,
                                    const ParserContext& context);
    std::string decode_uri_path(const std::string& uri);
    std::string normalize_path(const std::string& decoded_path);
    std::string decode_uri_query(const std::string& uri);
    inline int hex_to_int(char c);
    void commit_header(HttpRequest* request, const ParserContext& context);

    // Prevent copying
    RequestParser(const RequestParser&);
    RequestParser& operator=(const RequestParser&);

};  // class RequestParser

struct ParserContext {
    ParserContext() { reset(); }

    ParserState parser_state_;

    unsigned int granular_parser_state_;  // Current state of the request parser
    unsigned int return_state_;           // State to return to after parsing

    size_t
        total_bytes_processed_;  // Total bytes processed in the current request
    size_t chunk_remaining_bytes_;  // Remaining bytes in the current chunk

    const char* method_start_;
    const char* method_end_;
    const char* uri_start_;
    const char* uri_end_;
    const char* path_start_;
    const char* path_end_;
    const char* query_start_;
    const char* query_end_;
    const char* key_start_;
    const char* key_end_;
    const char* value_start_;
    const char* value_end_;
    int version_major_;
    int version_minor_;

    // TODO: Split states by when they should be reset
    void reset() {
        parser_state_ = PARSER_READING_REQUEST_LINE;
        granular_parser_state_ = 0;
        return_state_ = 0;
        chunk_remaining_bytes_ = 0;
        method_start_ = NULL;
        method_end_ = NULL;
        uri_start_ = NULL;
        uri_end_ = NULL;
        path_start_ = NULL;
        path_end_ = NULL;
        query_start_ = NULL;
        query_end_ = NULL;
        key_start_ = NULL;
        key_end_ = NULL;
        value_start_ = NULL;
        value_end_ = NULL;
        version_major_ = 0;
        version_minor_ = 0;
    }

};  // class ParserContext

enum ParserState {
    PARSER_READING_REQUEST_LINE,     // Parsing the request line (method, URI,
                                     // version)
    PARSER_READING_HEADERS,          // Parsing headers
    PARSER_PROCESSING_REQUEST,       // Processing headers after parsing
    PARSER_READING_CONTENT_BODY,     // Reading the body (if present)
    PARSER_READING_CHUNKED_BODY,     // Reading chunked body
    PARSER_DECODING_MULTIPART_BODY,  // Decoding multipart body
    PARSER_COMPLETE,                 // Request parsing complete
    PARSER_ERROR                     // An error occurred during parsing
};

enum ParseStatus {
    PARSE_INCOMPLETE,             // Need more data
    PARSE_SUCCESS,                // Request fully parsed
    PARSE_ERROR,                  // General parsing error
    PARSE_INVALID_REQUEST_LINE,   // Invalid request line
    PARSE_METHOD_NOT_ALLOWED,     // Unsupported HTTP method
    PARSE_INVALID_PATH,           // Invalid path in URI
    PARSE_INVALID_QUERY_STRING,   // Invalid query string in URI
    PARSE_VERSION_NOT_SUPPORTED,  // Unsupported HTTP version
    PARSE_REQUEST_TOO_LONG,       // Request exceeds maximum length
    PARSE_MISSING_HOST_HEADER,    // Host header is missing on HTTP/1.1 requests
    PARSE_HEADER_TOO_LONG,        // Header exceeds maximum length
    PARSE_TOO_MANY_HEADERS,       // Too many headers
    PARSE_MISSING_CONTENT_LENGTH,  // Content-Length or Transfer-encoding header
                                   // is missing
    PARSE_INVALID_CONTENT_LENGTH,  // Content-Length header is invalid
    PARSE_CONTENT_TOO_LARGE,       // Content length exceeds maximum
    PARSE_UNKNOWN_ENCODING,        // Unknown or unimplemented transfer encoding
    PARSE_INVALID_CHUNK_SIZE       // Invalid chunk size in chunked encoding
};

#endif  // REQUESTPARSER_HPP