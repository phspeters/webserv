#ifndef WEBSERV_HPP
#define WEBSERV_HPP

// TODO - fix header includes
#include <cstddef>
#include <ctime>

namespace http_limits {
const time_t TIMEOUT = 60;                    // Timeout in seconds
const size_t MAX_METHOD_LENGTH = 8;           // HTTP method length
const size_t MAX_REQUEST_LINE_LENGTH = 1024;  // 1KB
const size_t MAX_REQUEST_HEAD_LENGTH = 4096;  // 4KB
const size_t MAX_HEADER_NAME_LENGTH = 256;    // Header name length
const size_t MAX_HEADER_VALUE_LENGTH = 1024;  // Header value length
const size_t MAX_HEADERS = 100;               // Maximum number of headers
const size_t MAX_CONTENT_LENGTH = 8388608;    // 8MB
const size_t MAX_CHUNK_SIZE = 1048576;        // 1MB
}  // namespace http_limits

#define CRLF "\r\n"  // Carriage return + line feed
#define DEFAULT_CHUNK_SIZE 4096

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
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "AHandler.hpp"
#include "Buffer.hpp"
#include "ErrorHandler.hpp"
#include "RequestParser.hpp"
#include "VirtualServer.hpp"

#include "CgiHandler.hpp"
#include "Connection.hpp"
#include "ConnectionManager.hpp"
#include "FileDeleteHandler.hpp"
#include "FileUploadHandler.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "Logger.hpp"
#include "ResponseWriter.hpp"
#include "StaticFileHandler.hpp"
#include "WebServer.hpp"

// utils
std::string trim(const std::string& str);
std::string get_status_message(int code);

#endif