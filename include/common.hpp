#ifndef COMMON_HPP
#define COMMON_HPP

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
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#define CRLF "\r\n"  // Carriage return + line feed
#define DEFAULT_CHUNK_SIZE 4096
#define ACTIVE_LOG_LEVEL LOG_DEBUG

namespace http_limits {
const time_t TIMEOUT = 30;                    // Timeout in seconds
const size_t MAX_METHOD_LENGTH = 8;           // HTTP method length
const size_t MAX_REQUEST_LINE_LENGTH = 1024;  // 1KB
const size_t MAX_REQUEST_HEAD_LENGTH = 4096;  // 4KB
const size_t MAX_HEADER_NAME_LENGTH = 256;    // Header name length
const size_t MAX_HEADER_VALUE_LENGTH = 1024;  // Header value length
const size_t MAX_HEADERS = 100;               // Maximum number of headers
const size_t MAX_CONTENT_LENGTH = 8388608;    // 8MB
const size_t MAX_CHUNK_SIZE = 1048576;        // 1MB
}  // namespace http_limits

enum log_level {
    LOG_OFF,
    LOG_TRACE,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_FATAL
};

enum ConnectionState {
    CONN_READING_REQUEST,
    CONN_GENERATING_RESPONSE,
    CONN_WRITING_RESPONSE,
    CONN_FINISHING_WRITE,
    CONN_ERROR
};

enum ParserState {
    PARSER_READING_REQUEST_LINE,
    PARSER_READING_HEADERS,
    PARSER_PROCESSING_REQUEST,
    PARSER_READING_CONTENT_BODY,
    PARSER_READING_CHUNKED_BODY,
    PARSER_COMPLETE,
    PARSER_ERROR
};

enum CgiHandlerState {
    CGI_IDLE,
    CGI_WRITING_TO_PIPE,
    CGI_READING_FROM_PIPE,
    CGI_HEADERS_PARSED,
    CGI_COMPLETE,
    CGI_ERROR
};

enum WriterState {
    WRITER_START,
    WRITER_WRITING_HEADERS,
    WRITER_DECIDE_BODY_SOURCE, // New decider state
    WRITER_WRITING_BODY_FROM_BUFFER,
    WRITER_WRITING_BODY_FROM_FD,
    WRITER_DONE
};

enum ParseStatus {
    PARSE_INCOMPLETE,
    PARSE_SUCCESS,
    PARSE_ERROR,
    PARSE_INVALID_REQUEST_LINE,
    PARSE_METHOD_NOT_IMPLEMENTED,
    PARSE_METHOD_NOT_ALLOWED,
    PARSE_INVALID_PATH,
    PARSE_INVALID_QUERY_STRING,
    PARSE_VERSION_NOT_SUPPORTED,
    PARSE_REQUEST_TOO_LONG,
    PARSE_MISSING_HOST_HEADER,
    PARSE_HEADER_TOO_LONG,
    PARSE_TOO_MANY_HEADERS,
    PARSE_MISSING_CONTENT_LENGTH,
    PARSE_INVALID_CONTENT_LENGTH,
    PARSE_CONTENT_TOO_LARGE,
    PARSE_UNKNOWN_ENCODING,
    PARSE_INVALID_CHUNK_SIZE
};

enum WriteStatus { WRITE_SUCCESS, WRITE_INCOMPLETE, WRITE_ERROR };

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
    HEADER_TOO_LONG = 431,         // Request header fields too large

    // 5xx - Server Errors
    INTERNAL_SERVER_ERROR = 500,  // Generic server error
    NOT_IMPLEMENTED = 501,        // Server does not support the functionality
    BAD_GATEWAY = 502,  // Server acting as gateway received invalid response
    SERVICE_UNAVAILABLE = 503,  // Server temporarily unavailable
    GATEWAY_TIMEOUT = 504,  //  Gateway server did not receive response in time
    HTTP_VERSION_NOT_SUPPORTED = 505,  // HTTP version in request not supported
    INSUFFICIENT_STORAGE = 507         // Insufficient Storage
};

enum FdType {
    FD_LISTENER,
    FD_CLIENT_SOCKET,
    FD_CGI_PIPE_READ,
    FD_CGI_PIPE_WRITE,
    FD_FILE_UPLOAD,
    FD_STATIC_FILE,
};

#include "AHandler.hpp"
#include "Buffer.hpp"
#include "ErrorHandler.hpp"
#include "RequestParser.hpp"
#include "ResponseWriter.hpp"
#include "VirtualServer.hpp"

#include "CgiHandler.hpp"
#include "Connection.hpp"
#include "FileDeleteHandler.hpp"
#include "FileUploadHandler.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "Logger.hpp"
#include "StaticFileHandler.hpp"
#include "WebServer.hpp"

std::string trim(const std::string& str);
std::string get_status_message(int code);
std::string get_current_gmt_time();

#endif