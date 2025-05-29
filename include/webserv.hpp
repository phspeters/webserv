#ifndef WEBSERV_HPP
#define WEBSERV_HPP

// TODO - fix header includes
#include <cstddef>
#include <ctime>

namespace codes {
enum ConnectionState {
    CONN_READING,     // Waiting for/reading request data
    CONN_PROCESSING,  // Request received, handler is processing
    CONN_CGI_EXEC,    // Special state for active CGI execution
    CONN_WRITING,     // Handler generated response, sending data
    CONN_ERROR        // Connection encountered an error
};

// Represents the current state of the parser
enum ParserState {
    PARSING_REQUEST_LINE,  // Combines method, URI, HTTP version
    PARSING_HEADERS,       // All header parsing
    PARSING_BODY,          // Normal body
    PARSING_CHUNKED_BODY,  // Chunked transfer encoding
    PARSING_COMPLETE       // Request fully parsed
};

enum CgiHandlerState {
    CGI_HANDLER_IDLE,
    CGI_HANDLER_WRITING_TO_PIPE,   // Writing request body to CGI stdin
    CGI_HANDLER_READING_FROM_PIPE, // Reading response from CGI stdout
    CGI_HANDLER_COMPLETE,          // CGI script finished, response (or error) captured
    CGI_HANDLER_ERROR
};

enum WriterState {
    WRITING_COMPLETE,    // Response fully sent
    WRITING_INCOMPLETE,  // Partial write, needs another EPOLLOUT event
    WRITING_ERROR        // Error occurred during writing
};

// Result of a parsing attempt
enum ParseStatus {
    PARSE_HEADERS_COMPLETE,       // Headers parsed
    PARSE_SUCCESS,                // Request fully parsed
    PARSE_INCOMPLETE,             // Need more data
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

enum ResponseStatus {
    // 2xx - Success
    OK = 200,          // Request succeeded
    CREATED = 201,     // Request succeeded and a new resource was created
    NO_CONTENT = 204,  // Request succeeded but returns no content

    // 3xx - Redirection
    MOVED_PERMANENTLY = 301,  // Resource permanently moved to a new URL
    FOUND = 302,              // Resource temporarily moved to a new URL
    NOT_MODIFIED = 304,  // Resource hasn't been modified since last request

    // 4xx - Client Errors
    BAD_REQUEST = 400,   // Server cannot process the request (syntax error)
    UNAUTHORIZED = 401,  // Authentication required
    FORBIDDEN = 403,     // Server understood but refuses to authorize
    NOT_FOUND = 404,     // Resource not found
    METHOD_NOT_ALLOWED = 405,  // Request method not supported
    REQUEST_TIMEOUT = 408,     // Server timed out waiting for request
    CONFLICT = 409,            // Request conflict with current state of server
    LENGTH_REQUIRED = 411,     // Content-Length required but not provided
    PAYLOAD_TOO_LARGE = 413,   // Request entity too large
    URI_TOO_LONG = 414,        //  Request URI too long
    UNSUPPORTED_MEDIA_TYPE = 415,  // Media format not supported

    // 5xx - Server Errors
    INTERNAL_SERVER_ERROR = 500,  // Generic server error
    NOT_IMPLEMENTED = 501,        // Server does not support the functionality
    BAD_GATEWAY = 502,  // Server acting as gateway received invalid response
    SERVICE_UNAVAILABLE = 503,  // Server temporarily unavailable
    GATEWAY_TIMEOUT = 504,  //  Gateway server did not receive response in time
    HTTP_VERSION_NOT_SUPPORTED = 505  // HTTP version in request not supported
};

// Upload-specific error types
enum UploadError {
    UPLOAD_SUCCESS,            // Upload completed successfully
    UPLOAD_BAD_REQUEST,        // General 400 errors
    UPLOAD_FORBIDDEN,          // 403 errors
    UPLOAD_UNSUPPORTED_MEDIA,  // 415 errors
    UPLOAD_PAYLOAD_TOO_LARGE,  // 413 errors
    UPLOAD_SERVER_ERROR,        // 500 errors
    UPLOAD_INSUFFICIENT_STORAGE  // 507 Insufficient Storage
};

}  // namespace codes

namespace http_limits {
const time_t TIMEOUT = 60;                    // Timeout in seconds
const size_t MAX_METHOD_LENGTH = 8;           // HTTP method length
const size_t MAX_REQUEST_LINE_LENGTH = 8192;  // 8KB
const size_t MAX_PATH_LENGTH = 2048;          // Path component
const size_t MAX_QUERY_LENGTH = 2048;         // Query string
const size_t MAX_FRAGMENT_LENGTH = 1024;      // Fragment identifier
const size_t MAX_HEADER_NAME_LENGTH = 256;    // Header name length
const size_t MAX_HEADER_VALUE_LENGTH = 8192;  // Header value length
const size_t MAX_HEADERS = 100;               // Maximum number of headers
const size_t MAX_CONTENT_LENGTH = 10485760;   // 10MB
const size_t MAX_CHUNK_SIZE = 1048576;        // 1MB
}  // namespace http_limits

#define CRLF "\r\n"  // Carriage return + line feed

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "AHandler.hpp"
#include "ErrorHandler.hpp"
#include "RequestParser.hpp"
#include "VirtualServer.hpp"

#include "CgiHandler.hpp"
#include "Connection.hpp"
#include "ConnectionManager.hpp"
#include "FileUploadHandler.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "Logger.hpp"
#include "ResponseWriter.hpp"
#include "StaticFileHandler.hpp"
#include "WebServer.hpp"

// utils
std::string trim(const std::string& str);

#endif