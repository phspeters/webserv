#include "webserv.hpp"

RequestParser::RequestParser(const ServerConfig& config)
    : current_state_(PARSING_REQUEST_LINE),
      config_(config),
      chunk_remaining_bytes_(0) {}

RequestParser::~RequestParser() {}

RequestParser::ParseResult RequestParser::parse(Connection* conn) {
    // Reset parser state if we are starting a new request
    if (current_state_ == PARSING_COMPLETE) {
        reset_parser_state();
    }

    RequestParser::ParseResult result = PARSE_INCOMPLETE;

    // Process as much as possible in one call
    while (!conn->read_buffer_.empty()) {
        // Process based on current state
        // The state transition is handled inside each parsing function
        switch (current_state_) {
            case PARSING_REQUEST_LINE:
                result = parse_request_line(conn);
                break;
            case PARSING_HEADERS:
                result = parse_headers(conn);
                break;
            case PARSING_BODY:
                result = parse_body(conn);
                break;
            case PARSING_CHUNKED_BODY:
                result = parse_chunked_body(conn);
                break;
            case PARSING_COMPLETE:
                return PARSE_SUCCESS;
        }

        if (result != PARSE_INCOMPLETE) {
            // Either parsing is complete or an error occurred - return to
            // caller
            return result;
        }
    }

    return PARSE_INCOMPLETE;  // More data needed
}

RequestParser::ParseResult RequestParser::parse_request_line(Connection* conn) {
    std::vector<char>& buffer = conn->read_buffer_;
    HttpRequest* request = conn->request_data_;

    // Find the end of the request line (CRLF)
    std::vector<char>::iterator line_end =
        std::search(buffer.begin(), buffer.end(), CRLF, &CRLF[2]);

    if (line_end == buffer.end()) {
        // Not enough data yet
        if (buffer.size() > HttpConfig::MAX_REQUEST_LINE_LENGTH) {
            return PARSE_URI_TOO_LONG;
        }
        return PARSE_INCOMPLETE;
    }

    // Get the complete request line
    std::string request_line(buffer.begin(), line_end);

    // Parse method, URI, and version
    size_t first_space = request_line.find(' ');
    size_t last_space = request_line.rfind(' ');

    if (first_space == std::string::npos || last_space == first_space) {
        return PARSE_ERROR;
    }

    // Extract components
    request->method_ = request_line.substr(0, first_space);
    request->uri_ =
        request_line.substr(first_space + 1, last_space - first_space - 1);
    request->version_ = request_line.substr(last_space + 1);

    // Validate components
    ParseResult validation_result = validate_request_line(
        request->method_, request->uri_, request->version_);
    if (validation_result != PARSE_SUCCESS) {
        return validation_result;
    }

    // Remove processed data (including CRLF)
    buffer.erase(buffer.begin(), line_end + 2);

    // Move to header parsing
    current_state_ = PARSING_HEADERS;
    return PARSE_INCOMPLETE;
}

RequestParser::ParseResult RequestParser::validate_request_line(
    const std::string& method, const std::string& uri,
    const std::string& version) {
    if (!validate_method(method)) {
        return PARSE_METHOD_NOT_ALLOWED;
    }

    if (!validate_http_version(version)) {
        return PARSE_VERSION_NOT_SUPPORTED;
    }

    if (uri.length() > HttpConfig::MAX_URI_LENGTH) {
        return PARSE_URI_TOO_LONG;
    }

    if (!validate_uri(uri)) {
        return PARSE_ERROR;
    }

    return PARSE_SUCCESS;
}

bool RequestParser::validate_method(const std::string& method) {
    static const std::string methods[] = {"GET", "POST", "PUT", "DELETE",
                                          "HEAD"};
    static const std::vector<std::string> valid_methods(
        methods, methods + sizeof(methods) / sizeof(methods[0]));

    return std::find(valid_methods.begin(), valid_methods.end(), method) !=
           valid_methods.end();
}

bool RequestParser::validate_uri(const std::string& uri) {
    // Reject directory traversal
    if (uri.find("..") != std::string::npos) {
        return false;
    }

    // Check for invalid characters
    for (size_t i = 0; i < uri.length(); i++) {
        unsigned char c = static_cast<unsigned char>(uri[i]);
        if (c < 32 || c == 127) {
            return false;
        }
    }

    // Basic percent-encoding validation
    size_t pos = 0;
    while ((pos = uri.find('%', pos)) != std::string::npos) {
        if (pos + 2 >= uri.length() || !isxdigit(uri[pos + 1]) ||
            !isxdigit(uri[pos + 2])) {
            return false;
        }
        pos += 3;
    }

    return true;
}

bool RequestParser::validate_http_version(const std::string& version) {
    return version == "HTTP/1.0" || version == "HTTP/1.1";
}

RequestParser::ParseResult RequestParser::parse_headers(Connection* conn) {
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
            if (buffer.size() > HttpConfig::MAX_HEADER_VALUE_LENGTH) {
                return PARSE_HEADER_TOO_LONG;
            }
            return PARSE_INCOMPLETE;
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
        ParseResult result = process_single_header(header_line, request);

        if (result != PARSE_SUCCESS) {
            return result;
        }

        // Remove processed line (including CRLF)
        buffer.erase(buffer.begin(), line_end + 2);
    }

    // Headers are complete, determine next parser state
    if (headers_complete) {
        return determine_request_body_handling(request);
    }

    return PARSE_INCOMPLETE;
}

// Helper function to process a single header line
RequestParser::ParseResult RequestParser::process_single_header(
    const std::string& header_line, HttpRequest* request) {
    // Find the colon
    size_t colon_pos = header_line.find(':');
    if (colon_pos == std::string::npos || colon_pos == 0) {
        return PARSE_ERROR;
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
            return PARSE_ERROR;
        }

        key[i] = std::tolower(c);
    }

    // Trim leading whitespace from value
    size_t value_start = value.find_first_not_of(" \t");
    if (value_start != std::string::npos) {
        value = value.substr(value_start);
    }

    // Check header count limit
    if (request->headers_.size() >= HttpConfig::MAX_HEADERS) {
        return PARSE_TOO_MANY_HEADERS;
    }

    // Store the header
    request->headers_[key] = value;
    return PARSE_SUCCESS;
}

// Helper function to handle end-of-headers processing
RequestParser::ParseResult RequestParser::determine_request_body_handling(
    HttpRequest* request) {
    // Check for request body
    if (request->method_ == "POST" || request->method_ == "PUT") {
        // Check for Transfer-Encoding header
        std::string transfer_encoding =
            request->get_header("transfer-encoding");
        if (!transfer_encoding.empty() &&
            transfer_encoding.find("chunked") != std::string::npos) {
            current_state_ = PARSING_CHUNKED_BODY;
            return PARSE_INCOMPLETE;
        }

        // Check for Content-Length header
        std::string content_length = request->get_header("content-length");
        if (!content_length.empty()) {
            char* end_ptr;
            size_t body_size =
                std::strtoul(content_length.c_str(), &end_ptr, 10);

            // Validate content length format
            if (end_ptr == content_length.c_str() || *end_ptr != '\0') {
                return PARSE_INVALID_CONTENT_LENGTH;
            }

            if (body_size > HttpConfig::MAX_BODY_SIZE) {
                return PARSE_CONTENT_TOO_LARGE;
            }

            if (body_size > 0) {
                current_state_ = PARSING_BODY;
                return PARSE_INCOMPLETE;
            }
        }
    }

    // No body needed or zero-length body
    current_state_ = PARSING_COMPLETE;
    return PARSE_SUCCESS;
}

RequestParser::ParseResult RequestParser::parse_body(Connection* conn) {
    std::vector<char>& buffer = conn->read_buffer_;
    HttpRequest* request = conn->request_data_;

    // Get the expected body size from Content-Length
    std::string content_length = request->get_header("Content-Length");
    size_t body_size = std::strtoul(content_length.c_str(), NULL, 10);

    // Check if we have enough data
    if (buffer.size() < body_size) {
        return PARSE_INCOMPLETE;
    }

    // Copy body data
    request->body_.assign(buffer.begin(), buffer.begin() + body_size);

    // Remove processed data
    buffer.erase(buffer.begin(), buffer.begin() + body_size);

    // Request is complete
    current_state_ = PARSING_COMPLETE;
    return PARSE_SUCCESS;
}

// Chunked transfer encoding sends HTTP message bodies in a series of "chunks"
// without needing to know the total size in advance. Each chunk has a size
// prefix in hexadecimal notation, followed by the chunk data.
RequestParser::ParseResult RequestParser::parse_chunked_body(Connection* conn) {
    std::vector<char>& buffer = conn->read_buffer_;
    HttpRequest* request = conn->request_data_;
    ParseResult result;

    while (!buffer.empty()) {
        // Process based on chunked parsing state
        if (chunk_remaining_bytes_ == 0) {
            // Reading a new chunk size
            result = parse_chunk_header(buffer, chunk_remaining_bytes_);

            if (result != PARSE_SUCCESS) {
                return result;  // Either incomplete or error
            }

            // If chunk size is 0, this is the last chunk
            if (chunk_remaining_bytes_ == 0) {
                return finish_chunked_parsing(buffer);
            }

            continue;  // Process the chunk data in the next iteration
        }

        // Reading chunk data
        result = read_chunk_data(buffer, request, chunk_remaining_bytes_);

        if (result != PARSE_SUCCESS) {
            return result;
        }

        // If we've completed reading this chunk data
        if (chunk_remaining_bytes_ == 0) {
            result = process_chunk_terminator(buffer);

            if (result != PARSE_SUCCESS) {
                return result;
            }
        }
    }

    // Need more data
    return PARSE_INCOMPLETE;
}

// Helper method for parsing chunk headers
RequestParser::ParseResult RequestParser::parse_chunk_header(
    std::vector<char>& buffer, size_t& out_chunk_size) {
    // Find the end of the chunk size line
    std::vector<char>::iterator line_end =
        std::search(buffer.begin(), buffer.end(), CRLF, &CRLF[2]);

    if (line_end == buffer.end()) {
        return PARSE_INCOMPLETE;  // Need more data
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
        return PARSE_INVALID_CHUNK_SIZE;
    }

    // Validate chunk size
    if (out_chunk_size > HttpConfig::MAX_CHUNK_SIZE) {
        return PARSE_INVALID_CHUNK_SIZE;
    }

    // Remove chunk size line
    buffer.erase(buffer.begin(), line_end + 2);

    return PARSE_SUCCESS;
}

// Helper method for reading chunk data
RequestParser::ParseResult RequestParser::read_chunk_data(
    std::vector<char>& buffer, HttpRequest* request, size_t& remaining_bytes) {
    // Calculate how much data we can process
    size_t bytes_to_read = std::min(remaining_bytes, buffer.size());

    if (bytes_to_read == 0) {
        return PARSE_INCOMPLETE;
    }

    // Validate total body size
    if (request->body_.size() + bytes_to_read > HttpConfig::MAX_BODY_SIZE) {
        return PARSE_CONTENT_TOO_LARGE;
    }

    // Append directly to body
    request->body_.insert(request->body_.end(), buffer.begin(),
                          buffer.begin() + bytes_to_read);

    // Remove processed data
    buffer.erase(buffer.begin(), buffer.begin() + bytes_to_read);
    remaining_bytes -= bytes_to_read;

    return PARSE_SUCCESS;
}

// Helper method for processing chunk terminator (CRLF)
RequestParser::ParseResult RequestParser::process_chunk_terminator(
    std::vector<char>& buffer) {
    // Need CRLF after chunk data
    if (buffer.size() < 2) {
        return PARSE_INCOMPLETE;
    }

    // Verify and consume CRLF
    if (buffer[0] != '\r' || buffer[1] != '\n') {
        return PARSE_ERROR;
    }

    buffer.erase(buffer.begin(), buffer.begin() + 2);
    return PARSE_SUCCESS;
}

// Helper method for finishing chunked parsing (last chunk)
RequestParser::ParseResult RequestParser::finish_chunked_parsing(
    std::vector<char>& buffer) {
    // Process trailers line-by-line until we find an empty line
    while (true) {
        // Find the end of the current line
        std::vector<char>::iterator line_end =
            std::search(buffer.begin(), buffer.end(), CRLF, &CRLF[2]);

        if (line_end == buffer.end()) {
            return PARSE_INCOMPLETE;  // Need more data
        }

        // Check if this is an empty line (just CRLF)
        if (line_end == buffer.begin()) {
            // This is the end marker - remove it and finish
            buffer.erase(buffer.begin(), buffer.begin() + 2);
            current_state_ = PARSING_COMPLETE;
            return PARSE_SUCCESS;
        }

        // Otherwise, this is a trailer field - which we ignore

        // Remove the processed line
        buffer.erase(buffer.begin(), line_end + 2);
    }
}

void RequestParser::reset_parser_state() {
    current_state_ = PARSING_REQUEST_LINE;
    chunk_remaining_bytes_ = 0;
}
