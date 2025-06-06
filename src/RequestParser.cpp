#include "webserv.hpp"

RequestParser::RequestParser() {}

RequestParser::~RequestParser() {}

bool RequestParser::read_from_socket(Connection* conn) {
    log(LOG_DEBUG, "Reading from socket (fd: %i)", conn->client_fd_);

    // Resize the read buffer to accommodate incoming data
    size_t original_size = conn->read_buffer_.size();
    conn->read_buffer_.resize(original_size + CHUNK_SIZE);

    // Read data from the client socket
    ssize_t bytes_read = recv(
        conn->client_fd_, &conn->read_buffer_[original_size], CHUNK_SIZE, 0);

    if (bytes_read == 0) {
        // Connection closed by client
        log(LOG_WARNING, "Client disconnected (fd: %i)", conn->client_fd_);
        return false;
    }

    if (bytes_read < 0) {
        log(LOG_ERROR, "Error reading from socket (fd: %i): %s",
            conn->client_fd_, strerror(errno));
        return false;
    }

    // Update the last activity timestamp
    conn->last_activity_ = time(NULL);

    // Resize the buffer to the actual size read
    conn->read_buffer_.resize(original_size + bytes_read);

    log(LOG_DEBUG, "Read %zd bytes from socket (fd: %i)", bytes_read,
        conn->client_fd_);

    return true;
}

codes::ParseStatus RequestParser::parse(Connection* conn) {
    log(LOG_DEBUG, "Starting parsing attempt on Connection '%i'",
        conn->client_fd_);

    if (conn->parser_state_ == codes::PARSING_COMPLETE) {
        return codes::PARSE_SUCCESS;  // Already complete
    }

    codes::ParseStatus parse_status = codes::PARSE_INCOMPLETE;
    while (!conn->read_buffer_.empty() &&
           conn->parser_state_ != codes::PARSING_COMPLETE) {
        // Process based on current state
        // The state transition is handled inside each parsing function
        switch (conn->parser_state_) {
            case codes::PARSING_REQUEST_LINE:
                parse_status = parse_request_line(conn);
                // Modification - Carol    
                if(parse_status >= codes::PARSE_METHOD_NOT_ALLOWED && parse_status <= codes::PARSE_VERSION_NOT_SUPPORTED)
                    return parse_status;
                break;
            case codes::PARSING_HEADERS:
                // If headers are complete, return a special status for host
                // resolution
                parse_status = parse_headers(conn);
                break;

            case codes::PARSING_BODY:
                parse_status = parse_body(conn);
                break;

            case codes::PARSING_CHUNKED_BODY:
                parse_status = parse_chunked_body(conn);
                break;

            case codes::PARSING_COMPLETE:
                parse_status = codes::PARSE_SUCCESS;
                break;
        }
    }

    log(LOG_DEBUG,
        "Finished parsing attempt on Connection '%i' with status: %i",
        conn->client_fd_, parse_status);

    return parse_status;
}

codes::ParseStatus RequestParser::parse_request_line(Connection* conn) {
    log(LOG_DEBUG, "Parsing request line for connection: %i", conn->client_fd_);

    std::vector<char>& buffer = conn->read_buffer_;
    HttpRequest* request = conn->request_data_;

    // Find the end of the request line (CRLF)
    std::vector<char>::iterator line_end =
        std::search(buffer.begin(), buffer.end(), CRLF, &CRLF[2]);

    if (line_end == buffer.end()) {
        // Not enough data yet
        if (buffer.size() > http_limits::MAX_REQUEST_LINE_LENGTH) {
            log(LOG_ERROR, "Request line too long for connection: %i",
                conn->client_fd_);
            return codes::PARSE_REQUEST_TOO_LONG;
        }
        log(LOG_DEBUG, "Request line incomplete for connection: %i",
            conn->client_fd_);
        return codes::PARSE_INCOMPLETE;
    }

    // Get the complete request line
    std::string request_line(buffer.begin(), line_end);
    if (!split_request_line(request, request_line)) {
        return codes::PARSE_INVALID_REQUEST_LINE;
    }

    // Validate components
    codes::ParseStatus validation_status = validate_request_line(request);
    if (validation_status != codes::PARSE_SUCCESS) {
        // Return the specific validation error
        return validation_status;
    }

    // Remove processed data (including CRLF)
    buffer.erase(buffer.begin(), line_end + 2);

    // Move to header parsing
    conn->parser_state_ = codes::PARSING_HEADERS;
    log(LOG_DEBUG, "Request line parsed successfully for connection: %i",
        conn->client_fd_);

    return codes::PARSE_INCOMPLETE;
}

bool RequestParser::split_request_line(HttpRequest* request,
                                       std::string& request_line) {
    // Parse method, URI, and version
    size_t first_space = request_line.find(' ');
    size_t last_space = request_line.rfind(' ');

    if (first_space == std::string::npos || last_space == first_space) {
        log(LOG_ERROR, "Invalid request line format: '%s'",
            request_line.c_str());
        return false;
    }

    // Extract components
    request->method_ = request_line.substr(0, first_space);
    request->uri_ =
        request_line.substr(first_space + 1, last_space - first_space - 1);
    request->version_ = request_line.substr(last_space + 1);

    if (!split_uri_components(request)) {
        return false;
    }

    return true;
}

bool RequestParser::split_uri_components(HttpRequest* request) {
    std::string uri = request->uri_;

    // Check for query string ('?' delimiter)
    size_t query_pos = uri.find('?');
    if (query_pos != std::string::npos) {
        request->path_ = decode_uri_path(uri.substr(0, query_pos));
        if (request->path_.empty()) {
            log(LOG_ERROR, "Invalid path in URI: '%s'", uri.c_str());
            return false;  // Invalid path
        }
        request->query_string_ =
            decode_uri_query(request->uri_.substr(query_pos + 1));
        if (request->query_string_.empty()) {
            log(LOG_ERROR, "Invalid query string in URI: '%s'", uri.c_str());
            return false;  // Invalid query string
        }
    } else {
        request->path_ = decode_uri_path(request->uri_);
        if (request->path_.empty()) {
            log(LOG_ERROR, "Invalid path in URI: '%s'", uri.c_str());
            return false;  // Invalid path
        }
        request->query_string_.clear();
    }

    return true;
}

std::string RequestParser::decode_uri_path(const std::string& uri) {
    std::string decoded_uri;
    decoded_uri.reserve(uri.length());  // Performance optimization

    for (size_t i = 0; i < uri.length(); i++) {
        if (uri[i] == '%') {
            // Enhanced validation
            if (i + 2 >= uri.length()) {
                return "";  // Invalid encoding
            }

            char hex1 = uri[i + 1];
            char hex2 = uri[i + 2];

            if (!isxdigit(hex1) || !isxdigit(hex2)) {
                return "";  // Invalid hex digits
            }

            // Convert and validate
            int value = (hex_to_int(hex1) << 4) | hex_to_int(hex2);

            // Reject null bytes and control characters
            if (value == 0 || (value > 0 && value < 32) || value == 127) {
                return "";
            }

            // Security: Reject double-encoded attempts
            if (value == '%') {
                return "";  // Potential double-encoding attack
            }

            decoded_uri += static_cast<char>(value);
            i += 2;
        } else {
            // Validate regular characters
            unsigned char c = static_cast<unsigned char>(uri[i]);
            if (c < 32 || c == 127) {
                return "";  // Control characters not allowed
            }
            decoded_uri += uri[i];
        }
    }

    log(LOG_DEBUG, "Decoded URI path: '%s'", decoded_uri.c_str());
    return decoded_uri;
}

std::string RequestParser::decode_uri_query(const std::string& uri) {
    std::string decoded_uri;
    decoded_uri.reserve(uri.length());  // Performance optimization

    for (size_t i = 0; i < uri.length(); i++) {
        if (uri[i] == '%') {
            // Enhanced validation
            if (i + 2 >= uri.length()) {
                return "";  // Invalid encoding
            }

            char hex1 = uri[i + 1];
            char hex2 = uri[i + 2];

            if (!isxdigit(hex1) || !isxdigit(hex2)) {
                return "";  // Invalid hex digits
            }

            // Convert and validate
            int value = (hex_to_int(hex1) << 4) | hex_to_int(hex2);

            // Reject null bytes and control characters
            if (value == 0 || (value > 0 && value < 32) || value == 127) {
                return "";
            }

            // Security: Reject double-encoded attempts
            if (value == '%') {
                return "";  // Potential double-encoding attack
            }

            decoded_uri += static_cast<char>(value);
            i += 2;
        } else if (uri[i] == '+') {
            // Only convert + to space in query strings, not paths
            decoded_uri += ' ';
        } else {
            // Validate regular characters
            unsigned char c = static_cast<unsigned char>(uri[i]);
            if (c < 32 || c == 127) {
                return "";  // Control characters not allowed
            }
            decoded_uri += uri[i];
        }
    }

    log(LOG_DEBUG, "Decoded URI query: '%s'", decoded_uri.c_str());
    return decoded_uri;
}

codes::ParseStatus RequestParser::validate_request_line(
    const HttpRequest* request) {
    if (!validate_method(request->method_)) {
        log(LOG_WARNING, "Invalid HTTP method: '%s'", request->method_.c_str());
        return codes::PARSE_METHOD_NOT_ALLOWED;
    }

    if (!validate_path(request->path_)) {
        log(LOG_WARNING, "Invalid path in request: '%s'",
            request->path_.c_str());
        return codes::PARSE_INVALID_PATH;
    }

    if (!validate_query_string(request->query_string_)) {
        log(LOG_WARNING, "Invalid query string in request: '%s'",
            request->query_string_.c_str());
        return codes::PARSE_INVALID_QUERY_STRING;
    }

    if (!validate_http_version(request->version_)) {
        log(LOG_WARNING, "Unsupported HTTP version: '%s'",
            request->version_.c_str());
        return codes::PARSE_VERSION_NOT_SUPPORTED;
    }

    log(LOG_DEBUG, "Request line validated successfully: '%s %s %s'",
        request->method_.c_str(), request->path_.c_str(),
        request->version_.c_str());

    return codes::PARSE_SUCCESS;
}

int RequestParser::hex_to_int(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }

    log(LOG_FATAL, "Invalid hex character: '%c'", c);

    return -1;  // Should never reach here due to isxdigit check
}

bool RequestParser::validate_method(const std::string& method) {
    static const std::string methods[] = {
        "GET",
        "POST",
        "DELETE",
    };
    static const std::vector<std::string> valid_methods(
        methods, methods + sizeof(methods) / sizeof(methods[0]));

    return std::find(valid_methods.begin(), valid_methods.end(), method) !=
           valid_methods.end();
}

bool RequestParser::validate_path(const std::string& path) {
    // If path is empty, it's invalid
    if (path.empty()) {
        log(LOG_WARNING, "Empty path in request");
        return false;
    }

    // Check for length limit
    if (path.size() > http_limits::MAX_PATH_LENGTH) {
        log(LOG_WARNING, "Path too long in request: '%s'", path.c_str());
        return false;
    }

    // Must start with a slash
    if (path[0] != '/') {
        log(LOG_WARNING, "Path must start with a slash: '%s'", path.c_str());
        return false;
    }

    // Check for multiple consecutive slashes (e.g. "//home")
    if (path.find("//") != std::string::npos) {
        log(LOG_WARNING, "Path contains multiple consecutive slashes: '%s'",
            path.c_str());
        return false;
    }

    // Check each path segment for path traversal
    std::string segment;
    std::istringstream path_stream(path.substr(1));  // Skip leading '/'
    while (std::getline(path_stream, segment, '/')) {
        if (segment == ".." || segment == "." || segment.empty()) {
            log(LOG_WARNING, "Path contains invalid segment: '%s'",
                segment.c_str());
            return false;
        }
    }

    // According to RFC 3986, path should only contain:
    // - path = *( pchar / "/" )
    // - pchar = unreserved / pct-encoded / sub-delims / ":" / "@"
    // This means we should check that characters are limited to:
    // - Alphanumeric: a-z, A-Z, 0-9
    // - Unreserved: -, ., _, ~
    // - Sub-delimiters: !, $, &, ', (, ), *, +, ,, ;, =
    // - Additional characters: :, @, /
    for (size_t i = 0; i < path.length(); ++i) {
        unsigned char c = static_cast<unsigned char>(path[i]);

        // Allow these characters
        if (isalnum(c) || strchr("-._~!$&'()*+,;=:@/", c)) {
            continue;
        }

        // Reject any other characters
        log(LOG_WARNING, "Invalid character in path: '%c' in '%s'", c,
            path.c_str());
        return false;
    }

    log(LOG_DEBUG, "Path validated successfully: '%s'", path.c_str());
    return true;
}

bool RequestParser::validate_query_string(const std::string& query_string) {
    // If query string is empty, it's valid
    if (query_string.empty()) {
        log(LOG_DEBUG, "Empty query string is valid");
        return true;
    }

    // Check for length limit
    if (query_string.size() > http_limits::MAX_QUERY_LENGTH) {
        log(LOG_WARNING, "Query string too long: '%s'", query_string.c_str());
        return false;
    }

    // According to RFC 3986, query strings should only contain:
    // - query string = *( pchar / "/" / "?" )
    // - pchar = unreserved / pct-encoded / sub-delims / ":" / "@"
    // This means we should check that characters are limited to:
    // - Alphanumeric: a-z, A-Z, 0-9
    // - Unreserved: -, ., _, ~
    // - Sub-delimiters: !, $, &, ', (, ), *, +, ,, ;, =
    // - Additional characters: :, @, /, ?
    for (size_t i = 0; i < query_string.length(); ++i) {
        unsigned char c = static_cast<unsigned char>(query_string[i]);

        // Allow these characters
        if (isalnum(c) || strchr("-._~!$&'()*+,;=:@/?", c)) {
            continue;
        }

        // Reject any other characters
        log(LOG_WARNING, "Invalid character in query string: '%c' in '%s'", c,
            query_string.c_str());
        return false;
    }

    log(LOG_DEBUG, "Query string validated successfully: '%s'",
        query_string.c_str());
    return true;
}

bool RequestParser::validate_http_version(const std::string& version) {
    return version == "HTTP/1.0" || version == "HTTP/1.1";
}

codes::ParseStatus RequestParser::parse_headers(Connection* conn) {
    log(LOG_DEBUG, "Parsing headers for connection: %i", conn->client_fd_);
    std::vector<char>& buffer = conn->read_buffer_;
    HttpRequest* request = conn->request_data_;

    // Process headers until we find an empty line or need more data
    bool headers_complete = false;

    while (!headers_complete && !buffer.empty()) {
        // Find the end of the current header line
        std::vector<char>::iterator line_end =
            std::search(buffer.begin(), buffer.end(), CRLF, &CRLF[2]);

        // Need more data?
        if (line_end == buffer.end()) {
            // Check header size limit before requesting more data
            if (buffer.size() > http_limits::MAX_HEADER_VALUE_LENGTH) {
                log(LOG_ERROR, "Header value too long for connection: %i",
                    conn->client_fd_);
                return codes::PARSE_HEADER_TOO_LONG;
            }
            log(LOG_DEBUG, "Headers parsing incomplete for connection: %i",
                conn->client_fd_);
            return codes::PARSE_INCOMPLETE;
        }

        // Check for empty line (end of headers)
        if (line_end == buffer.begin()) {
            // Remove CRLF and mark headers as complete
            buffer.erase(buffer.begin(), buffer.begin() + 2);
            headers_complete = true;
            break;
        }

        // Process a normal header line
        std::string header_line(buffer.begin(), line_end);
        codes::ParseStatus parse_status =
            process_single_header(header_line, request);

        if (parse_status != codes::PARSE_SUCCESS) {
            log(LOG_ERROR, "Failed to parse header '%s' for connection: %i",
                header_line.c_str(), conn->client_fd_);
            return parse_status;
        }

        // Remove processed line (including CRLF)
        buffer.erase(buffer.begin(), line_end + 2);
    }

    // Headers are complete, determine next parser state
    if (headers_complete) {
        log(LOG_DEBUG, "Finished headers parsing for connection: %i",
            conn->client_fd_);
        codes::ParseStatus parse_status = validate_headers(conn);
        if (parse_status != codes::PARSE_SUCCESS) {
            log(LOG_ERROR,
                "Header validation failed for connection: %i with status: %i",
                conn->client_fd_, parse_status);
            return parse_status;
        }
        return determine_request_body_handling(conn);
    }

    log(LOG_DEBUG, "Headers parsing incomplete for connection: %i",
        conn->client_fd_);
    return codes::PARSE_INCOMPLETE;
}

// Helper function to process a single header line
codes::ParseStatus RequestParser::process_single_header(
    const std::string& header_line, HttpRequest* request) {
    // Find the colon
    size_t colon_pos = header_line.find(':');
    if (colon_pos == std::string::npos || colon_pos == 0) {
        return codes::PARSE_ERROR;
    }

    // Extract key and value
    std::string key = header_line.substr(0, colon_pos);
    std::string value = header_line.substr(colon_pos + 1);

    // Check for invalid characters in key and convert to lowercase
    for (size_t i = 0; i < key.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(key[i]);

        // RFC 7230: field-name = token
        // token = 1*tchar
        // tchar = "!" / "#" / "$" / "%" / "&" / "'" / "*" / "+" / "-" / "." /
        //         "^" / "_" / "`" / "|" / "~" / DIGIT / ALPHA
        if (!(isalnum(c) || strchr("!#$%&'*+-.^_`|~", c))) {
            return codes::PARSE_ERROR;
        }

        key[i] = std::tolower(c);
    }

    // Trim leading whitespace from value
    size_t value_start = value.find_first_not_of(" \t");
    if (value_start != std::string::npos) {
        value = value.substr(value_start);
    }

    // Check header count limit
    if (request->headers_.size() >= http_limits::MAX_HEADERS) {
        return codes::PARSE_TOO_MANY_HEADERS;
    }

    // Store the header
    request->set_header(key, value);
    return codes::PARSE_SUCCESS;
}

// Helper function to handle end-of-headers processing
codes::ParseStatus RequestParser::determine_request_body_handling(
    Connection* conn) {
    HttpRequest* request = conn->request_data_;
    // Check for request body
    if (request->method_ == "POST" || request->method_ == "PUT") {
        // Check for Transfer-Encoding header
        std::string transfer_encoding =
            request->get_header("transfer-encoding");
        if (!transfer_encoding.empty() &&
            transfer_encoding.find("chunked") != std::string::npos) {
            conn->parser_state_ = codes::PARSING_CHUNKED_BODY;
            return codes::PARSE_HEADERS_COMPLETE;
        }

        // Check for Content-Length header
        std::string content_length = request->get_header("content-length");
        if (!content_length.empty()) {
            char* end_ptr;
            size_t body_size =
                std::strtoul(content_length.c_str(), &end_ptr, 10);

            if (body_size > 0) {
                conn->parser_state_ = codes::PARSING_BODY;
                return codes::PARSE_HEADERS_COMPLETE;
            }
        }
    }

    // No body needed or zero-length body
    conn->parser_state_ = codes::PARSING_COMPLETE;
    return codes::PARSE_HEADERS_COMPLETE;
}

codes::ParseStatus RequestParser::validate_headers(Connection* conn) {
    // Host header required for HTTP/1.1
    HttpRequest* request = conn->request_data_;
    if (request->version_ == "HTTP/1.1" &&
        request->get_header("host").empty()) {
        // Translates to response status 400
        log(LOG_ERROR,
            "Missing Host header in HTTP/1.1 request for connection: %i",
            conn->client_fd_);
        return codes::PARSE_MISSING_HOST_HEADER;
    }

    if (request->method_ == "POST" || request->method_ == "PUT") {
        bool has_content_length =
            !request->get_header("content-length").empty();
        bool has_transfer_encoding =
            !request->get_header("transfer-encoding").empty();

        if (!has_content_length && !has_transfer_encoding) {
            // Translates to response status 411
            log(LOG_ERROR,
                "POST/PUT without Content-Length or Transfer-Encoding");
            return codes::PARSE_MISSING_CONTENT_LENGTH;
        }

        if (has_content_length && has_transfer_encoding) {
            // Translates to response status 400
            log(LOG_ERROR,
                "POST/PUT with both Content-Length and Transfer-Encoding");
            return codes::PARSE_INVALID_CONTENT_LENGTH;
        }

        if (has_content_length) {
            // Validate Content-Length
            std::string content_length = request->get_header("content-length");
            char* end_ptr;
            size_t body_size =
                std::strtoul(content_length.c_str(), &end_ptr, 10);

            // Check for invalid Content-Length format
            if (end_ptr == content_length.c_str() || *end_ptr != '\0') {
                log(LOG_ERROR, "Invalid Content-Length header: '%s'",
                    content_length.c_str());
                // Translates to response status 400
                return codes::PARSE_INVALID_CONTENT_LENGTH;
            }

            if (body_size > conn->virtual_server_->client_max_body_size_) {
                log(LOG_ERROR, "Content-Length exceeds maximum size: %zu",
                    body_size);
                // Translates to response status 413
                return codes::PARSE_CONTENT_TOO_LARGE;
            }
        }

        if (has_transfer_encoding) {
            // Validate Transfer-Encoding
            std::string transfer_encoding =
                request->get_header("transfer-encoding");
            if (transfer_encoding != "chunked") {
                log(LOG_ERROR, "Unknown Transfer-Encoding: '%s'",
                    transfer_encoding.c_str());
                // Translates to response status 501
                return codes::PARSE_UNKNOWN_ENCODING;
            }
        }
    }

    log(LOG_DEBUG, "Headers validated successfully for connection: %i",
        conn->client_fd_);
    return codes::PARSE_SUCCESS;
}

codes::ParseStatus RequestParser::parse_body(Connection* conn) {
    log(LOG_DEBUG, "Parsing body for connection: %i", conn->client_fd_);

    std::vector<char>& buffer = conn->read_buffer_;
    HttpRequest* request = conn->request_data_;

    // Get the expected body size from Content-Length
    std::string content_length = request->get_header("content-length");
    size_t body_size = std::strtoul(content_length.c_str(), NULL, 10);

    // Check if we have enough data
    if (buffer.size() < body_size) {
        log(LOG_DEBUG, "Body parsing incomplete for connection: %i",
            conn->client_fd_);
        return codes::PARSE_INCOMPLETE;
    }

    // Copy body data
    request->body_.assign(buffer.begin(), buffer.begin() + body_size);

    // Remove processed data
    buffer.erase(buffer.begin(), buffer.begin() + body_size);

    // Request is complete
    log(LOG_DEBUG, "Body parsed successfully for connection: %i",
        conn->client_fd_);
    conn->parser_state_ = codes::PARSING_COMPLETE;
    return codes::PARSE_SUCCESS;
}

// Chunked transfer encoding sends HTTP message bodies in a series of "chunks"
// without needing to know the total size in advance. Each chunk has a size
// prefix in hexadecimal notation, followed by the chunk data.
codes::ParseStatus RequestParser::parse_chunked_body(Connection* conn) {
    log(LOG_DEBUG, "Parsing chunked body for connection: %i", conn->client_fd_);

    std::vector<char>& buffer = conn->read_buffer_;
    HttpRequest* request = conn->request_data_;
    codes::ParseStatus parse_status;

    while (!buffer.empty()) {
        // Process based on chunked parsing state
        if (conn->chunk_remaining_bytes_ == 0) {
            // Reading a new chunk size
            parse_status =
                parse_chunk_header(buffer, conn->chunk_remaining_bytes_);

            if (parse_status != codes::PARSE_SUCCESS) {
                log(LOG_ERROR,
                    "Failed to parse chunk header for "
                    "connection: %i with status: %i",
                    conn->client_fd_, parse_status);
                return parse_status;  // Either incomplete or error
            }

            // If chunk size is 0, this is the last chunk
            if (conn->chunk_remaining_bytes_ == 0) {
                codes::ParseStatus status = finish_chunked_parsing(buffer);
                if (status == codes::PARSE_SUCCESS) {
                    log(LOG_DEBUG,
                        "Chunked body parsing complete for connection: %i",
                        conn->client_fd_);
                    conn->parser_state_ = codes::PARSING_COMPLETE;
                }
                return status;
            }

            continue;  // Process the chunk data in the next iteration
        }

        // Reading chunk data
        parse_status =
            read_chunk_data(buffer, request, conn->chunk_remaining_bytes_,
                            conn->virtual_server_->client_max_body_size_);

        if (parse_status != codes::PARSE_SUCCESS) {
            log(LOG_ERROR,
                "Failed to read chunk data for connection: %i with status: %i",
                conn->client_fd_, parse_status);
            return parse_status;
        }

        // If we've completed reading this chunk data
        if (conn->chunk_remaining_bytes_ == 0) {
            parse_status = process_chunk_terminator(buffer);

            if (parse_status != codes::PARSE_SUCCESS) {
                log(LOG_ERROR,
                    "Failed to process chunk terminator for "
                    "connection: %i with status: %i",
                    conn->client_fd_, parse_status);
                return parse_status;
            }
        }
    }

    // Need more data
    log(LOG_DEBUG, "Chunked body parsing incomplete for connection: %i",
        conn->client_fd_);
    return codes::PARSE_INCOMPLETE;
}

// Helper method for parsing chunk headers
codes::ParseStatus RequestParser::parse_chunk_header(std::vector<char>& buffer,
                                                     size_t& out_chunk_size) {
    // Find the end of the chunk size line
    std::vector<char>::iterator line_end =
        std::search(buffer.begin(), buffer.end(), CRLF, &CRLF[2]);

    if (line_end == buffer.end()) {
        log(LOG_DEBUG, "Chunk header parsing incomplete, need more data");
        return codes::PARSE_INCOMPLETE;  // Need more data
    }

    // Parse the chunk size (hex)
    std::string chunk_size_line(buffer.begin(), line_end);

    // Remove any chunk extensions (after semicolon)
    size_t semicolon = chunk_size_line.find(';');
    if (semicolon != std::string::npos) {
        chunk_size_line = chunk_size_line.substr(0, semicolon);
    }

    // Trim leading/trailing whitespace
    chunk_size_line.erase(0, chunk_size_line.find_first_not_of(" \t"));
    chunk_size_line.erase(chunk_size_line.find_last_not_of(" \t") + 1);

    // Parse hex value
    char* end_ptr;
    out_chunk_size = std::strtoul(chunk_size_line.c_str(), &end_ptr, 16);

    // Ensure valid hex number
    if (end_ptr == chunk_size_line.c_str() ||
        (*end_ptr != '\0' && *end_ptr != ' ' && *end_ptr != '\t')) {
        log(LOG_ERROR, "Invalid chunk size format: '%s'",
            chunk_size_line.c_str());
        return codes::PARSE_INVALID_CHUNK_SIZE;
    }

    // Validate chunk size
    if (out_chunk_size > http_limits::MAX_CHUNK_SIZE) {
        log(LOG_ERROR, "Chunk size exceeds maximum limit: %zu", out_chunk_size);
        return codes::PARSE_INVALID_CHUNK_SIZE;
    }

    // Remove chunk size line
    buffer.erase(buffer.begin(), line_end + 2);

    log(LOG_DEBUG, "Parsed chunk size: %zu for connection: %i", out_chunk_size,
        buffer.size());
    return codes::PARSE_SUCCESS;
}

// Helper method for reading chunk data
codes::ParseStatus RequestParser::read_chunk_data(std::vector<char>& buffer,
                                                  HttpRequest* request,
                                                  size_t& remaining_bytes,
                                                  size_t client_max_body_size) {
    // Calculate how much data we can process
    size_t bytes_to_read = std::min(remaining_bytes, buffer.size());

    if (bytes_to_read == 0) {
        log(LOG_DEBUG, "No chunk data to read");
        return codes::PARSE_INCOMPLETE;
    }

    // Validate total body size
    if (request->body_.size() + bytes_to_read > client_max_body_size) {
        log(LOG_ERROR, "Chunked body exceeds maximum size: %zu",
            request->body_.size() + bytes_to_read);
        return codes::PARSE_CONTENT_TOO_LARGE;
    }

    // Append directly to body
    request->body_.insert(request->body_.end(), buffer.begin(),
                          buffer.begin() + bytes_to_read);

    // Remove processed data
    buffer.erase(buffer.begin(), buffer.begin() + bytes_to_read);
    remaining_bytes -= bytes_to_read;

    log(LOG_DEBUG, "Read %zu bytes of chunk data for connection: %i",
        bytes_to_read, request->body_.size());
    return codes::PARSE_SUCCESS;
}

// Helper method for processing chunk terminator (CRLF)
codes::ParseStatus RequestParser::process_chunk_terminator(
    std::vector<char>& buffer) {
    // Need CRLF after chunk data
    if (buffer.size() < 2) {
        log(LOG_DEBUG, "Chunk terminator incomplete, need more data");
        return codes::PARSE_INCOMPLETE;
    }

    // Verify and consume CRLF
    if (buffer[0] != '\r' || buffer[1] != '\n') {
        log(LOG_ERROR, "Invalid chunk terminator, expected CRLF");
        return codes::PARSE_ERROR;
    }

    buffer.erase(buffer.begin(), buffer.begin() + 2);
    log(LOG_TRACE, "Processed chunk terminator for connection: %i",
        buffer.size());
    return codes::PARSE_SUCCESS;
}

// Helper method for finishing chunked parsing (last chunk)
codes::ParseStatus RequestParser::finish_chunked_parsing(
    std::vector<char>& buffer) {
    // Process trailers line-by-line until we find an empty line
    while (true) {
        // Find the end of the current line
        std::vector<char>::iterator line_end =
            std::search(buffer.begin(), buffer.end(), CRLF, &CRLF[2]);

        if (line_end == buffer.end()) {
            log(LOG_DEBUG,
                "Chunked trailers parsing incomplete, need more data");
            return codes::PARSE_INCOMPLETE;  // Need more data
        }

        // Check if this is an empty line (just CRLF)
        if (line_end == buffer.begin()) {
            // This is the end marker - remove it and finish
            buffer.erase(buffer.begin(), buffer.begin() + 2);
            log(LOG_DEBUG,
                "Chunked trailers parsing complete for connection: %i",
                buffer.size());
            return codes::PARSE_SUCCESS;
        }

        // Otherwise, this is a trailer field - which we ignore

        // Remove the processed line
        buffer.erase(buffer.begin(), line_end + 2);
    }
}
