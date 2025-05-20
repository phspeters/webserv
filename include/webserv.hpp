#ifndef WEBSERV_HPP
#define WEBSERV_HPP

//TODO - fix header includes
#include <cstddef>
#include <ctime>

namespace codes {
enum ConnectionState {
    CONN_READING,     // Waiting for/reading request data
    CONN_PROCESSING,  // Request received, handler is processing
    CONN_WRITING,     // Handler generated response, sending data
    CONN_CGI_EXEC,    // Special state for active CGI execution
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

// Result of a parsing attempt
enum ParseStatus {
    PARSE_SUCCESS,                 // Request fully parsed
    PARSE_INCOMPLETE,              // Need more data
    PARSE_ERROR,                   // General parsing error
    PARSE_INVALID_REQUEST_LINE,    // Invalid request line
    PARSE_METHOD_NOT_ALLOWED,      // Unsupported HTTP method
    PARSE_INVALID_PATH,            // Invalid path in URI
    PARSE_INVALID_QUERY_STRING,    // Invalid query string in URI
    PARSE_VERSION_NOT_SUPPORTED,   // Unsupported HTTP version
    PARSE_REQUEST_TOO_LONG,        // Request exceeds maximum length
    PARSE_HEADER_TOO_LONG,         // Header exceeds maximum length
    PARSE_TOO_MANY_HEADERS,        // Too many headers
    PARSE_INVALID_CONTENT_LENGTH,  // Content-Length header is invalid
    PARSE_CONTENT_TOO_LARGE,       // Content length exceeds maximum
    PARSE_INVALID_CHUNK_SIZE       // Invalid chunk size in chunked encoding
};

enum ResponseStatus {
    RESPONSE_COMPLETE,     // Response fully sent
    RESPONSE_INCOMPLETE,   // Partial write, needs another EPOLLOUT event
    RESPONSE_ERROR         // Error occurred during writing
};
}  // namespace codes

namespace http_limits {
const time_t TIMEOUT = 60;  // Timeout in seconds	
const size_t MAX_METHOD_LENGTH = 8;
const size_t MAX_REQUEST_LINE_LENGTH = 8192;  // 8KB
const size_t MAX_PATH_LENGTH = 2048;          // Path component
const size_t MAX_QUERY_LENGTH = 2048;         // Query string
const size_t MAX_FRAGMENT_LENGTH = 1024;      // Fragment identifier
const size_t MAX_HEADER_NAME_LENGTH = 256;
const size_t MAX_HEADER_VALUE_LENGTH = 8192;
const size_t MAX_HEADERS = 100;
const size_t MAX_CONTENT_LENGTH = 10485760;  // 10MB
const size_t MAX_CHUNK_SIZE = 1048576;       // 1MB
}  // namespace http_limits

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>  

#include <algorithm>
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