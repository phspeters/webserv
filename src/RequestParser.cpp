#include "webserv.hpp"

RequestParser::RequestParser() {}

RequestParser::~RequestParser() {}

bool RequestParser::read_from_socket(Connection* conn) {
    // Resize the read buffer to accommodate incoming data
    size_t original_size = conn->read_buffer_.size();
    conn->read_buffer_.resize(original_size + 4096);

    // Read data from the client socket
    ssize_t bytes_read =
        recv(conn->client_fd_, &conn->read_buffer_[original_size], 4096, 0);

    if (bytes_read == 0) {
        // Connection closed by client
        std::cout << "Client disconnected (fd: " << conn->client_fd_ << ")"
                  << std::endl;
        return false;
    }

    if (bytes_read < 0) {
        // Handle error
        std::cerr << "Read error on socket (fd: " << conn->client_fd_ << ")"
                  << std::endl;
        return false;
    }

    // Update the last activity timestamp
    conn->last_activity_ = time(NULL);

    // Resize the buffer to the actual size read
    conn->read_buffer_.resize(original_size + bytes_read);

    return true;
}

codes::ParseStatus RequestParser::parse(Connection* conn) {
    codes::ParseStatus& parse_status = conn->parse_status_;

    // Process as much as possible in one call
    while (!conn->read_buffer_.empty()) {
        // Process based on current state
        // The state transition is handled inside each parsing function
        switch (conn->parser_state_) {
            case codes::PARSING_REQUEST_LINE:
                parse_status = parse_request_line(conn);
                break;
            case codes::PARSING_HEADERS:
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

        if (parse_status != codes::PARSE_INCOMPLETE) {
            // Either parsing is complete or an error occurred - return to
            // caller
            return parse_status;
        }
    }

    return codes::PARSE_INCOMPLETE;  // More data needed
}

codes::ParseStatus RequestParser::parse_request_line(Connection* conn) {
    std::vector<char>& buffer = conn->read_buffer_;
    HttpRequest* request = conn->request_data_;

    // Find the end of the request line (CRLF)
    std::vector<char>::iterator line_end =
        std::search(buffer.begin(), buffer.end(), CRLF, &CRLF[2]);

    if (line_end == buffer.end()) {
        // Not enough data yet
        if (buffer.size() > http_limits::MAX_REQUEST_LINE_LENGTH) {
            return codes::PARSE_REQUEST_TOO_LONG;
        }
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
        return validation_status;
    }

    // Remove processed data (including CRLF)
    buffer.erase(buffer.begin(), line_end + 2);

    // Move to header parsing
    conn->parser_state_ = codes::PARSING_HEADERS;
    return codes::PARSE_INCOMPLETE;
}

bool RequestParser::split_request_line(HttpRequest* request,
                                       std::string& request_line) {
    // Parse method, URI, and version
    size_t first_space = request_line.find(' ');
    size_t last_space = request_line.rfind(' ');

    if (first_space == std::string::npos || last_space == first_space) {
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
        request->path_ = decode_uri(uri.substr(0, query_pos));
        if (request->path_.empty()) {
            return false;  // Invalid path
        }
        request->query_string_ =
            decode_uri(request->uri_.substr(query_pos + 1));
        if (request->query_string_.empty()) {
            return false;  // Invalid query string
        }
    } else {
        request->path_ = decode_uri(request->uri_);
        if (request->path_.empty()) {
            return false;  // Invalid path
        }
        request->query_string_.clear();
    }

    return true;
}

std::string RequestParser::decode_uri(const std::string& uri) {
    std::string decoded_uri;

    for (size_t i = 0; i < uri.length(); i++) {
        if (uri[i] == '%') {
            // Check for valid percent-encoding
            if (i + 2 >= uri.length() || !isxdigit(uri[i + 1]) ||
                !isxdigit(uri[i + 2])) {
                return "";
            }

            // Convert hex value
            std::string hex = uri.substr(i + 1, 2);
            int value;
            std::istringstream(hex) >> std::hex >> value;

            // Check for control characters (which are not allowed)
            if (value < 32 || value == 127) {
                return "";
            }

            decoded_uri += static_cast<char>(value);
            i += 2;  // Skip the two hex digits
        } else if (uri[i] == '+') {
            // In query strings, '+' becomes space
            decoded_uri += ' ';
        } else {
            // Direct character (also check for control characters)
            if (static_cast<unsigned char>(uri[i]) < 32 ||
                static_cast<unsigned char>(uri[i]) == 127) {
                return "";
            }
            decoded_uri += uri[i];
        }
    }

    return decoded_uri;
}

codes::ParseStatus RequestParser::validate_request_line(
    const HttpRequest* request) {
    if (!validate_method(request->method_)) {
        return codes::PARSE_METHOD_NOT_ALLOWED;
    }

    if (!validate_path(request->path_)) {
        return codes::PARSE_INVALID_PATH;
    }

    if (!validate_query_string(request->query_string_)) {
        return codes::PARSE_INVALID_QUERY_STRING;
    }

    if (!validate_http_version(request->version_)) {
        return codes::PARSE_VERSION_NOT_SUPPORTED;
    }

    return codes::PARSE_SUCCESS;
}

bool RequestParser::validate_method(const std::string& method) {
    static const std::string METHODS[] = {
        "GET",
        "POST",
        "DELETE",
    };
    static const std::vector<std::string> VALID_METHODS(
        METHODS, METHODS + sizeof(METHODS) / sizeof(METHODS[0]));

    return std::find(VALID_METHODS.begin(), VALID_METHODS.end(), method) !=
           VALID_METHODS.end();
}

bool RequestParser::validate_path(const std::string& path) {
    // If path is empty, it's invalid
    if (path.empty()) {
        return false;
    }

    // Check for length limit
    if (path.size() > http_limits::MAX_PATH_LENGTH) {
        return false;
    }

    // Must start with a slash
    if (path[0] != '/') {
        return false;
    }

    // Check for multiple consecutive slashes (e.g. "//home")
    if (path.find("//") != std::string::npos) {
        return false;
    }

    // Check each path segment for path traversal
    std::string segment;
    std::istringstream path_stream(path.substr(1));  // Skip leading '/'
    while (std::getline(path_stream, segment, '/')) {
        if (segment == ".." || segment == "." || segment.empty()) {
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
        return false;
    }

    return true;
}

bool RequestParser::validate_query_string(const std::string& query_string) {
    // If query string is empty, it's valid
    if (query_string.empty()) {
        return true;
    }

    // Check for length limit
    if (query_string.size() > http_limits::MAX_QUERY_LENGTH) {
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
        return false;
    }

    return true;
}

bool RequestParser::validate_http_version(const std::string& version) {
    return version == "HTTP/1.0" || version == "HTTP/1.1";
}

codes::ParseStatus RequestParser::parse_headers(Connection* conn) {
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
                return codes::PARSE_HEADER_TOO_LONG;
            }
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
            return parse_status;
        }

        // Remove processed line (including CRLF)
        buffer.erase(buffer.begin(), line_end + 2);
    }

    // Headers are complete, determine next parser state
    if (headers_complete) {
        return determine_request_body_handling(conn);
    }

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
    request->headers_[key] = value;
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
            return codes::PARSE_INCOMPLETE;
        }

        // Check for Content-Length header
        std::string content_length = request->get_header("content-length");
        if (!content_length.empty()) {
            char* end_ptr;
            size_t body_size =
                std::strtoul(content_length.c_str(), &end_ptr, 10);

            // Validate content length format
            if (end_ptr == content_length.c_str() || *end_ptr != '\0') {
                return codes::PARSE_INVALID_CONTENT_LENGTH;
            }

            if (body_size > conn->virtual_server_->client_max_body_size_) {
                return codes::PARSE_CONTENT_TOO_LARGE;
            }

            if (body_size > 0) {
                conn->parser_state_ = codes::PARSING_BODY;
                return codes::PARSE_INCOMPLETE;
            }
        }
    }

    // No body needed or zero-length body
    conn->parser_state_ = codes::PARSING_COMPLETE;
    return codes::PARSE_SUCCESS;
}

codes::ParseStatus RequestParser::parse_body(Connection* conn) {
    std::vector<char>& buffer = conn->read_buffer_;
    HttpRequest* request = conn->request_data_;

    // Get the expected body size from Content-Length
    std::string content_length = request->get_header("Content-Length");
    size_t body_size = std::strtoul(content_length.c_str(), NULL, 10);

    // Check if we have enough data
    if (buffer.size() < body_size) {
        return codes::PARSE_INCOMPLETE;
    }

    // Copy body data
    request->body_.assign(buffer.begin(), buffer.begin() + body_size);

    // Remove processed data
    buffer.erase(buffer.begin(), buffer.begin() + body_size);

    // Request is complete
    conn->parser_state_ = codes::PARSING_COMPLETE;
    return codes::PARSE_SUCCESS;
}

// Chunked transfer encoding sends HTTP message bodies in a series of "chunks"
// without needing to know the total size in advance. Each chunk has a size
// prefix in hexadecimal notation, followed by the chunk data.
codes::ParseStatus RequestParser::parse_chunked_body(Connection* conn) {
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
                return parse_status;  // Either incomplete or error
            }

            // If chunk size is 0, this is the last chunk
            if (conn->chunk_remaining_bytes_ == 0) {
                codes::ParseStatus status = finish_chunked_parsing(buffer);
                if (status == codes::PARSE_SUCCESS) {
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
            return parse_status;
        }

        // If we've completed reading this chunk data
        if (conn->chunk_remaining_bytes_ == 0) {
            parse_status = process_chunk_terminator(buffer);

            if (parse_status != codes::PARSE_SUCCESS) {
                return parse_status;
            }
        }
    }

    // Need more data
    return codes::PARSE_INCOMPLETE;
}

// Helper method for parsing chunk headers
codes::ParseStatus RequestParser::parse_chunk_header(std::vector<char>& buffer,
                                                     size_t& out_chunk_size) {
    // Find the end of the chunk size line
    std::vector<char>::iterator line_end =
        std::search(buffer.begin(), buffer.end(), CRLF, &CRLF[2]);

    if (line_end == buffer.end()) {
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
        return codes::PARSE_INVALID_CHUNK_SIZE;
    }

    // Validate chunk size
    if (out_chunk_size > http_limits::MAX_CHUNK_SIZE) {
        return codes::PARSE_INVALID_CHUNK_SIZE;
    }

    // Remove chunk size line
    buffer.erase(buffer.begin(), line_end + 2);

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
        return codes::PARSE_INCOMPLETE;
    }

    // Validate total body size
    if (request->body_.size() + bytes_to_read > client_max_body_size) {
        return codes::PARSE_CONTENT_TOO_LARGE;
    }

    // Append directly to body
    request->body_.insert(request->body_.end(), buffer.begin(),
                          buffer.begin() + bytes_to_read);

    // Remove processed data
    buffer.erase(buffer.begin(), buffer.begin() + bytes_to_read);
    remaining_bytes -= bytes_to_read;

    return codes::PARSE_SUCCESS;
}

// Helper method for processing chunk terminator (CRLF)
codes::ParseStatus RequestParser::process_chunk_terminator(
    std::vector<char>& buffer) {
    // Need CRLF after chunk data
    if (buffer.size() < 2) {
        return codes::PARSE_INCOMPLETE;
    }

    // Verify and consume CRLF
    if (buffer[0] != '\r' || buffer[1] != '\n') {
        return codes::PARSE_ERROR;
    }

    buffer.erase(buffer.begin(), buffer.begin() + 2);
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
            return codes::PARSE_INCOMPLETE;  // Need more data
        }

        // Check if this is an empty line (just CRLF)
        if (line_end == buffer.begin()) {
            // This is the end marker - remove it and finish
            buffer.erase(buffer.begin(), buffer.begin() + 2);
            return codes::PARSE_SUCCESS;
        }

        // Otherwise, this is a trailer field - which we ignore

        // Remove the processed line
        buffer.erase(buffer.begin(), line_end + 2);
    }
}

void RequestParser::handle_parse_error(Connection* conn, codes::ParseStatus status) {
    int http_status;
    
    // Map parser errors to HTTP status codes
    switch (status) {
        case codes::PARSE_INVALID_REQUEST_LINE:
        case codes::PARSE_INVALID_PATH:
        case codes::PARSE_INVALID_QUERY_STRING:
        case codes::PARSE_INVALID_CHUNK_SIZE:
        case codes::PARSE_INVALID_CONTENT_LENGTH:
        case codes::PARSE_ERROR:
            http_status = 400; // Bad Request
            break;
            
        case codes::PARSE_METHOD_NOT_ALLOWED:
            http_status = 405; // Method Not Allowed
            break;
            
        case codes::PARSE_REQUEST_TOO_LONG:
        case codes::PARSE_HEADER_TOO_LONG:
        case codes::PARSE_TOO_MANY_HEADERS:
            http_status = 431; // Request Header Fields Too Large
            break;
            
        case codes::PARSE_VERSION_NOT_SUPPORTED:
            http_status = 505; // HTTP Version Not Supported
            break;
            
        case codes::PARSE_CONTENT_TOO_LARGE:
            http_status = 413; // Payload Too Large
            break;
            
        default:
            http_status = 400; // Default to Bad Request
    }
    
    // Apply the appropriate error response
    ErrorHandler::apply_to_connection(conn, http_status, *(conn->virtual_server_));
    
    // Update connection state to writing
    conn->conn_state_ = codes::CONN_WRITING;
}
