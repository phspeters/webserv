#include "webserv.hpp"

RequestParser::RequestParser() {}

RequestParser::~RequestParser() {}

bool RequestParser::read_from_socket(Connection* conn) {
    log(LOG_DEBUG, "Reading from socket (fd: %i)", conn->client_fd_);

    // Read data from the client socket
    ssize_t bytes_read = recv(conn->client_fd_, conn->read_buffer_.write_ptr(),
                              conn->read_buffer_.writable_space(), 0);

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

    // Move the last pointer forward by the number of bytes read
    conn->read_buffer_.has_written(bytes_read);

    // Update the last activity timestamp
    conn->last_activity_ = time(NULL);

    log(LOG_DEBUG, "Read %zd bytes from socket (fd: %i)", bytes_read,
        conn->client_fd_);

    log_buffer(LOG_TRACE, conn->read_buffer_);

    return true;
}

codes::ParseStatus RequestParser::parse_request_line(Connection* conn) {
    log(LOG_DEBUG, "Parsing request line for connection: %i", conn->client_fd_);

    enum ReqLineState {
        START,
        METHOD,
        SPACES_BEFORE_URI,
        URI_PATH,
        URI_QUERY,
        URI_PERCENT_ENCODING_1,
        URI_PERCENT_ENCODING_2,
        SPACES_AFTER_URI,
        HTTP_H,
        HTTP_HT,
        HTTP_HTT,
        HTTP_HTTP,
        HTTP_SLASH,
        HTTP_MAJOR_DIGIT,
        HTTP_DOT,
        HTTP_MINOR_DIGIT,
        VERSION_DONE,
        ALMOST_DONE,
    };

    Buffer& buff = conn->read_buffer_;
    unsigned int& state = conn->parser_context_.parser_state_;
    ParserContext& context = conn->parser_context_;

    // The main loop processes every available byte in the buffer.
    while (buff.readable_bytes() > 0) {
        // Look at the current character without consuming it yet.
        const char ch = *buff.read_ptr();

        switch (state) {
            case ReqLineState::START:
                if (ch == '\r' || ch == '\n') {
                    // Skip leading CRLF from some clients.
                    break;  // Character is consumed at the end of the loop.
                }

                // Check for a valid uppercase letter to start the method.
                if (ch < 'A' || ch > 'Z') {
                    return codes::PARSE_ERROR;  // Invalid start to a request.
                }

                // First valid character found. Mark the start of the method.
                context.method_start_ = buff.read_ptr();
                state = ReqLineState::METHOD;

                // Instead of falling through, continue the loop to re-evaluate
                // the same character with the new state.
                continue;  // This skips the buff.has_read(1) at the end

            case ReqLineState::METHOD:
                if (buff.read_ptr() - context.method_start_ >
                    http_limits::MAX_METHOD_LENGTH) {
                    return codes::PARSE_ERROR;
                }

                if (ch == ' ') {
                    // Trigger: Space ends the method. Mark the end.
                    context.method_end_ = buff.read_ptr();
                    state = ReqLineState::SPACES_BEFORE_URI;
                } else if (ch < 'A' || ch > 'Z') {
                    // Invalid character within the method.
                    return codes::PARSE_ERROR;
                }
                break;

            case ReqLineState::SPACES_BEFORE_URI:
                if (ch == ' ') {
                    // Continue skipping spaces.
                    break;  // Character is consumed at the end of the loop.
                }

                if (ch == '/') {
                    // Origin-Form
                    context.uri_start_ = buff.read_ptr();
                    context.path_start_ = buff.read_ptr();
                    state = ReqLineState::URI_PATH;
                    continue;
                } else {
                    // Invalid character after method.
                    return codes::PARSE_ERROR;
                }

            case ReqLineState::URI_PATH:
                if (ch == ' ') {
                    // Trigger: Space ends the URI. Mark the end.
                    context.uri_end_ = buff.read_ptr();
                    context.path_end_ = buff.read_ptr();
                    state = ReqLineState::HTTP_H;
                } else if (ch == '?') {
                    // Query starts. Mark the end of the path.
                    context.path_end_ = buff.read_ptr();
                    state = ReqLineState::URI_QUERY;
                } else if (!isalnum(ch) && !strchr("/-._~:!$&'()*+,;=@", ch)) {
                    // The only other valid thing is a percent-encoding
                    if (ch == '%') {
                        context.return_state_ = state;
                        state = ReqLineState::URI_PERCENT_ENCODING_1;
                    } else {
                        return codes::PARSE_ERROR;  // Invalid character
                    }
                }
                break;

            case ReqLineState::URI_PERCENT_ENCODING_1:
                // Expecting a hex digit after '%'
                if (isxdigit(ch)) {
                    state = ReqLineState::URI_PERCENT_ENCODING_2;
                } else {
                    return codes::PARSE_ERROR;  // Invalid percent-encoding
                }
                break;

            case ReqLineState::URI_PERCENT_ENCODING_2:
                // Expecting a second hex digit after the first one
                if (isxdigit(ch)) {
                    // Valid percent-encoding, continue parsing the URI
                    state = context.return_state_;  // Back to last state
                } else {
                    return codes::PARSE_ERROR;  // Invalid percent-encoding
                }
                break;

            case ReqLineState::URI_QUERY:
                if (ch == ' ') {
                    // Trigger: Space ends the URI. Mark the end.
                    context.uri_end_ = buff.read_ptr();
                    context.query_end_ = buff.read_ptr();
                    state = ReqLineState::HTTP_H;
                } else if (!isalnum(ch) && !strchr("/-._~:!$&'()*+,;=@?", ch)) {
                    // The only other valid thing is a percent-encoding
                    if (ch == '%') {
                        context.return_state_ = state;
                        state = ReqLineState::URI_PERCENT_ENCODING_1;
                    } else {
                        return codes::PARSE_ERROR;  // Invalid character
                    }
                }
                break;

            case ReqLineState::SPACES_AFTER_URI:
                if (ch == ' ') {
                    // Continue skipping spaces.
                    break;  // Character is consumed at the end of the loop.
                } else {
                    state = ReqLineState::HTTP_H;
                }
                break;

            case ReqLineState::HTTP_H:
                if (ch == 'H') {
                    state = ReqLineState::HTTP_HT;
                } else {
                    return codes::PARSE_ERROR;  // Invalid HTTP version start
                }
                break;

            case ReqLineState::HTTP_HT:
                if (ch == 'T') {
                    state = ReqLineState::HTTP_HTT;
                } else {
                    return codes::PARSE_ERROR;  // Invalid HTTP version start
                }
                break;

            case ReqLineState::HTTP_HTT:
                if (ch == 'T') {
                    state = ReqLineState::HTTP_HTTP;
                } else {
                    return codes::PARSE_ERROR;  // Invalid HTTP version start
                }
                break;

            case ReqLineState::HTTP_HTTP:
                if (ch == 'P') {
                    state = ReqLineState::HTTP_SLASH;
                } else {
                    return codes::PARSE_ERROR;  // Invalid HTTP version start
                }
                break;

            case ReqLineState::HTTP_SLASH:
                if (ch == '/') {
                    state = ReqLineState::HTTP_MAJOR_DIGIT;
                } else {
                    return codes::PARSE_ERROR;  // Invalid HTTP version start
                }
                break;

            case ReqLineState::HTTP_MAJOR_DIGIT:
                if (isdigit(ch)) {
                    context.version_major_ = ch - '0';
                    state = ReqLineState::HTTP_DOT;
                } else {
                    return codes::PARSE_ERROR;  // Invalid major version digit
                }
                break;

            case ReqLineState::HTTP_DOT:
                if (ch == '.') {
                    state = ReqLineState::HTTP_MINOR_DIGIT;
                } else {
                    return codes::PARSE_ERROR;  // Invalid character after major
                                                // version digit
                }
                break;

            case ReqLineState::HTTP_MINOR_DIGIT:
                if (isdigit(ch)) {
                    context.version_minor_ = ch - '0';
                    state = ReqLineState::VERSION_DONE;
                } else {
                    return codes::PARSE_ERROR;  // Invalid minor version digit
                }
                break;

            case ReqLineState::VERSION_DONE:
                if (ch == '\r') {
                    state = ReqLineState::ALMOST_DONE;
                }
                break;

            case ReqLineState::ALMOST_DONE:
                if (ch == '\n') {
                    // Final Trigger: We found LF after CR.
                    codes::ParseStatus status =
                        commit_request_line(conn->request_data_, context);

                    buff.has_read(1);           // Consume the final '\n'
                    context.parser_state_ = 0;  // Reset parser state

                    return status;
                }
                return codes::PARSE_ERROR;  // Invalid character after
                                            // CR.
        }

        // Consume the character and move to the next one in the buffer
        // for the next loop iteration.
        buff.has_read(1);
        if (conn->read_buffer_.processed_bytes() >
            http_limits::MAX_REQUEST_LINE_LENGTH) {
            log(LOG_WARNING, "Request line too long");
            return codes::PARSE_REQUEST_TOO_LONG;
        }
    }

    // If we exit the loop, it's because the buffer is empty. We need more
    // data.
    return codes::PARSE_INCOMPLETE;
}

codes::ParseStatus RequestParser::commit_request_line(
    HttpRequest* request, const ParserContext& context) {
    // 1. Assign raw components from the parsed pointers.
    request->method_.assign(context.method_start_,
                            context.method_end_ - context.method_start_);
    request->uri_.assign(context.uri_start_,
                         context.uri_end_ - context.uri_start_);

    std::stringstream version_stream;
    version_stream << "HTTP/" << context.version_major_ << "."
                   << context.version_minor_;
    request->version_ = version_stream.str();

    // 2. Decode and normalize the path component.
    if (context.path_start_ && context.path_end_) {
        std::string raw_path(context.path_start_,
                             context.path_end_ - context.path_start_);
        std::string decoded_path = decode_uri_path(raw_path);

        // A valid path can never be empty after decoding or normalization.
        if (decoded_path.empty()) {
            log(LOG_WARNING, "Path decoding failed for: '%s'",
                raw_path.c_str());
            return codes::PARSE_INVALID_PATH;
        }

        request->path_ = normalize_path(decoded_path);
        if (request->path_.empty()) {
            log(LOG_WARNING, "Path normalization failed for: '%s'",
                decoded_path.c_str());
            return codes::PARSE_INVALID_PATH;
        }
    } else {
        log(LOG_FATAL,
            "Path start or end pointers are not set: can't parse path");
    }

    // 3. Decode the query string component.
    if (context.query_start_ && context.query_end_) {
        std::string raw_query(context.query_start_,
                              context.query_end_ - context.query_start_);
        request->query_string_ = decode_uri_query(raw_query);

        // An empty query string is valid. However, if the raw query was not
        // empty but the decoded one is, it implies a decoding error.
        if (request->query_string_.empty() && !raw_query.empty()) {
            log(LOG_WARNING, "Query string decoding failed for: '%s'",
                raw_query.c_str());
            return codes::PARSE_INVALID_QUERY_STRING;
        }
    }

    log(LOG_DEBUG, "Parsed request line: %s %s %s", request->method_.c_str(),
        request->uri_.c_str(), request->version_.c_str());
    return codes::PARSE_SUCCESS;
}

std::string RequestParser::decode_uri_path(const std::string& path) {
    std::string decoded_path;
    decoded_path.reserve(path.length());  // Performance optimization

    for (size_t i = 0; i < path.length(); i++) {
        if (path[i] == '%') {
            if (i + 2 >= path.length()) {
                return "";  // Invalid percent-encoding sequence
            }

            char hex1 = path[i + 1];
            char hex2 = path[i + 2];

            // Convert and validate
            int value = (hex_to_int(hex1) << 4) | hex_to_int(hex2);

            // Reject any non-printable ASCII characters.
            if (value < ' ' || value > '~') {
                return "";
            }

            // Security: Reject double-encoded attempts
            if (value == '%') {
                return "";  // Potential double-encoding attack
            }

            decoded_path += static_cast<char>(value);
            i += 2;
        } else {
            decoded_path += path[i];
        }
    }

    log(LOG_DEBUG, "Decoded URI path: '%s'", decoded_path.c_str());
    return decoded_path;
}

std::string RequestParser::normalize_path(const std::string& decoded_path) {
    if (decoded_path.empty() || decoded_path[0] != '/') {
        return "";  // Invalid path
    }

    std::vector<std::string> segments;
    std::string current_segment;
    std::stringstream path_stream(decoded_path);

    // Split path by '/'
    while (std::getline(path_stream, current_segment, '/')) {
        if (current_segment.empty() || current_segment == ".") {
            // Ignore empty segments (from "//") or "." segments
            continue;
        }
        if (current_segment == "..") {
            // If not at the root, go up one level
            if (!segments.empty()) {
                segments.pop_back();
            }
        } else {
            // A normal segment
            segments.push_back(current_segment);
        }
    }

    // Reconstruct the canonical path
    std::string canonical_path = "/";
    if (!segments.empty()) {
        for (size_t i = 0; i < segments.size(); ++i) {
            canonical_path +=
                segments[i] + (i < segments.size() - 1 ? "/" : "");
        }
    }
    return canonical_path;
}

std::string RequestParser::decode_uri_query(const std::string& query) {
    std::string decoded_query;
    decoded_query.reserve(query.length());  // Performance optimization

    for (size_t i = 0; i < query.length(); i++) {
        if (query[i] == '%') {
            if (i + 2 >= query.length()) {
                return "";  // Invalid encoding
            }

            char hex1 = query[i + 1];
            char hex2 = query[i + 2];

            // Convert and validate
            int value = (hex_to_int(hex1) << 4) | hex_to_int(hex2);

            // Reject any non-printable ASCII characters.
            if (value < ' ' || value > '~') {
                return "";
            }

            // Security: Reject double-encoded attempts
            if (value == '%') {
                return "";  // Potential double-encoding attack
            }

            decoded_query += static_cast<char>(value);
            i += 2;
        } else if (query[i] == '+') {
            // Only convert + to space in query strings, not paths
            decoded_query += ' ';
        } else {
            decoded_query += query[i];
        }
    }

    log(LOG_DEBUG, "Decoded URI query: '%s'", decoded_query.c_str());
    return decoded_query;
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

codes::ParseStatus RequestParser::parse_headers(Connection* conn) {
    log(LOG_DEBUG, "Parsing headers for connection: %i", conn->client_fd_);

    enum HeadParseState {
        LINE_START,
        NAME,
        SPACE_BEFORE_VALUE,
        VALUE,
        VALUE_ALMOST_DONE,
        HEADERS_ALMOST_DONE,
    };

    Buffer& buff = conn->read_buffer_;
    unsigned int& state = conn->parser_context_.parser_state_;
    ParserContext& context = conn->parser_context_;

    // The main loop processes every available byte in the buffer.
    while (buff.readable_bytes() > 0) {
        // Look at the current character without consuming it yet.
        const char ch = *buff.read_ptr();

        switch (state) {
            case HeadParseState::LINE_START:
                // The first character should be either the start of a header
                // name or a CR.
                if (ch == '\r') {
                    state = HeadParseState::HEADERS_ALMOST_DONE;
                    break;
                }

                if (!is_token_char(ch)) {
                    // Invalid start to a header name.
                    return codes::PARSE_ERROR;
                }

                context.key_start_ = buff.read_ptr();
                state = HeadParseState::NAME;
                continue;  // This skips the buff.has_read(1) at the
                           // end of the loop

            case HeadParseState::NAME:
                if (ch == ':') {
                    if (buff.read_ptr() == context.key_start_) {
                        return codes::PARSE_ERROR;  // Empty header name
                    }
                    context.key_end_ = buff.read_ptr();
                    state = HeadParseState::SPACE_BEFORE_VALUE;
                } else if (!is_token_char(ch)) {
                    return codes::PARSE_ERROR;
                } else if (buff.read_ptr() - context.key_start_ >=
                           http_limits::MAX_HEADER_NAME_LENGTH) {
                    return codes::PARSE_ERROR;
                }
                break;

            case HeadParseState::SPACE_BEFORE_VALUE:
                // Skip optional whitespace (spaces and tabs) before the value.
                if (ch == ' ' || ch == '\t') {
                    break;
                }
                // An empty header value is allowed (e.g., "Header:\r\n").
                if (ch == '\r') {
                    context.value_start_ = NULL;
                    context.value_end_ = NULL;
                    state = HeadParseState::VALUE_ALMOST_DONE;
                    break;
                }
                // The first non-space character marks the start of the value.
                context.value_start_ = buff.read_ptr();
                state = HeadParseState::VALUE;
                continue;  // Re-evaluate this character in the new VALUE state.

            case HeadParseState::VALUE:
                if (ch == '\r') {
                    context.value_end_ = buff.read_ptr();
                    state = HeadParseState::VALUE_ALMOST_DONE;
                }

                // A header value can contain any visible ASCII character,
                // spaces, and horizontal tabs. It MUST NOT contain other
                // control characters. (RFC 7230, Section 3.2)
                if (iscntrl(ch) && ch != '\t') {
                    // This correctly rejects NUL, CR, LF, and all other CTL
                    // chars.
                    return codes::PARSE_ERROR;
                }

                if (buff.read_ptr() - context.value_start_ >
                    http_limits::MAX_HEADER_VALUE_LENGTH) {
                    return codes::PARSE_ERROR;  // Header value too long
                }
                // Any other character is part of the value.
                break;

            case HeadParseState::VALUE_ALMOST_DONE:
                if (ch == '\n') {
                    // We have seen CRLF. The header line is complete.
                    commit_header(conn->request_data_,
                                  context);     // A helper to add the
                                                // header to the map
                    context.key_start_ = NULL;  // Reset for the next header
                    context.key_end_ = NULL;
                    context.value_start_ = NULL;  // Reset for the next header
                    context.value_end_ = NULL;
                    state = HeadParseState::LINE_START;  // Ready for the
                                                         // next line
                } else {
                    return codes::PARSE_ERROR;  // Expected LF after CR
                }
                break;

            case HeadParseState::HEADERS_ALMOST_DONE:
                if (ch == '\n') {
                    // We have seen CRLF after the last header. Headers
                    // are done. Now we can determine how to handle the
                    // body.
                    buff.has_read(1);  // Consume last character
                    conn->parser_context_.parser_state_ = 0;  // Reset state
                    return codes::PARSE_SUCCESS;
                } else {
                    return codes::PARSE_ERROR;  // Expected LF after CR
                }
        }

        buff.has_read(1);  // Consume the character
        if (conn->read_buffer_.processed_bytes() >
            http_limits::MAX_REQUEST_HEAD_LENGTH) {
            log(LOG_WARNING, "Request headers too long");
            return codes::PARSE_HEADER_TOO_LONG;
        }
    }

    // If we exit the loop, it's because the buffer is empty. We need more
    // data.
    return codes::PARSE_INCOMPLETE;
}

// It checks if a character is a valid "tchar" according to RFC 7230.
bool is_token_char(char c) {
    // Check for alphanumeric characters
    if (isalnum(c)) {
        return true;
    }
    // Check against the list of allowed special characters.
    return strchr("!#$%&'*+-.^_`|~", c) != NULL;
}

void RequestParser::commit_header(HttpRequest* request,
                                  const ParserContext& context) {
    std::string header_name(context.key_start_,
                            context.key_end_ - context.key_start_);

    std::string header_value;
    if (context.value_start_ && context.value_end_) {
        header_value.assign(context.value_start_,
                            context.value_end_ - context.value_start_);
    }

    request->set_header(header_name, header_value);
}

codes::ParseStatus RequestParser::parse_body(Connection* conn) {
    log(LOG_DEBUG, "Parsing body for connection: %i", conn->client_fd_);

    HttpRequest* req = conn->request_data_;
    Buffer& buff = conn->read_buffer_;

    if (req->content_length_ == 0) {
        return codes::PARSE_SUCCESS;
    }

    // For efficiency, pre-reserve memory in the body vector to avoid
    // multiple reallocations
    if (req->body_.capacity() < req->content_length_) {
        req->body_.reserve(req->content_length_);
    }

    // Determine how many bytes we can and should move.
    size_t bytes_needed = req->content_length_ - req->body_.size();
    size_t bytes_available = buff.readable_bytes();
    size_t bytes_to_move = std::min(bytes_needed, bytes_available);

    if (bytes_to_move > 0) {
        // Append the data from the read buffer to the body vector.
        req->body_.insert(req->body_.end(), buff.read_ptr(),
                          buff.read_ptr() + bytes_to_move);

        // Correctly consume only the bytes that were moved from the read
        // buffer.
        buff.has_read(bytes_to_move);
        log(LOG_TRACE, "Moved %zu bytes from read buffer to request body.",
            bytes_to_move);
    }

    // Check if we have now read the entire body.
    if (req->body_.size() == req->content_length_) {
        log(LOG_DEBUG, "Body parsing complete for connection: %i",
            conn->client_fd_);
        return codes::PARSE_SUCCESS;
    }

    // If we reach here, it means we still need more data.
    log(LOG_DEBUG,
        "Body parsing incomplete for connection: %i, need %zu more bytes.",
        conn->client_fd_, req->content_length_ - req->body_.size());
    return codes::PARSE_INCOMPLETE;
}

// TODO ----------------------------------------------------------

// Chunked transfer encoding sends HTTP message bodies in a series of
// "chunks" without needing to know the total size in advance. Each chunk
// has a size prefix in hexadecimal notation, followed by the chunk data.
codes::ParseStatus RequestParser::parse_chunked_body(Connection* conn) {
    log(LOG_DEBUG, "Parsing chunked body for connection: %i", conn->client_fd_);

    enum ChunkParseState {
        CHUNK_START,
        CHUNK_SIZE,
        CHUNK_EXTENSION,
        CHUNK_EXTENSION_ALMOST_DONE,
        CHUNK_DATA,
        AFTER_DATA,
        AFTER_DATA_ALMOST_DONE,
        LAST_CHUNK,
        TRAILER,
        TRAILER_ALMOST_DONE,
        TRAILER_HEADER,
        TRAILER_HEADER_ALMOST_DONE
    };

    Buffer& buff = conn->read_buffer_;
    unsigned int& state = conn->parser_context_.parser_state_;
    ParserContext& context = conn->parser_context_;

    // The main loop processes every available byte in the buffer.
    while (buff.readable_bytes() > 0) {
        // Look at the current character without consuming it yet.
        const char ch = *buff.read_ptr();

        switch (state) {
            case ChunkParseState::CHUNK_START:
                if (ch == '\r') {
                    // Skip leading CRLF from some clients.
                    break;  // Character is consumed at the end of the loop.
                }

                if (!isxdigit(ch)) {
                    return codes::PARSE_INVALID_CHUNK_SIZE;  // Invalid start
                }

                context.value_start_ = buff.read_ptr();
                state = ChunkParseState::CHUNK_SIZE;
                continue;  // This skips the buff.has_read(1) at the end of
                           // the loop

            case ChunkParseState::CHUNK_SIZE:
                if (ch == ';') {
                    // Optional chunk extension starts.
                    context.value_end_ = buff.read_ptr();
                    state = ChunkParseState::CHUNK_EXTENSION;
                } else if (ch == '\r') {
                    // End of chunk size. Prepare to read data.
                    context.value_end_ = buff.read_ptr();
                    state = ChunkParseState::CHUNK_DATA;
                } else if (!isxdigit(ch)) {
                    return codes::PARSE_INVALID_CHUNK_SIZE;  // Invalid size
                }
                break;

            case ChunkParseState::CHUNK_EXTENSION:
                if (ch == '\r') {
                    context.value_end_ = buff.read_ptr();
                    state = ChunkParseState::CHUNK_EXTENSION_ALMOST_DONE;
                }
                break;

            case ChunkParseState::CHUNK_EXTENSION_ALMOST_DONE:
                if (ch == '\n') {
                    // We have seen CRLF after the chunk size and extension.
                    state = ChunkParseState::CHUNK_DATA;
                } else {
                    return codes::PARSE_ERROR;  // Expected LF after CR
                }
                break;

            case ChunkParseState::CHUNK_DATA:
                if (context.chunk_remaining_bytes_ == 0) {
                    // We need to read the chunk size next.
                    state = ChunkParseState::AFTER_DATA;
                    continue;  // Re-evaluate this character in the new state.
                }

                conn->request_data_->body_.push_back(ch);
                context.chunk_remaining_bytes_--;
                break;

            case ChunkParseState::AFTER_DATA:
                if (ch == '\r') {
                    // End of chunk data. Prepare to read the next chunk size.
                    state = ChunkParseState::AFTER_DATA_ALMOST_DONE;
                } else {
                    return codes::PARSE_ERROR;  // Expected CR after chunk data
                }
        }
    }

    // If we exit the loop, it's because the buffer is empty. We need more
    // data.
    log(LOG_DEBUG,
        "Chunked body parsing incomplete for connection: %i, need more data.",
        conn->client_fd_);
    return codes::PARSE_INCOMPLETE;
}
