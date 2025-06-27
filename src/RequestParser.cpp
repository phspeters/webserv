#include "webserv.hpp"

ParseStatus RequestParser::parse_request_line(Connection* conn) {
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
    unsigned int& state = conn->parser_context_.granular_parser_state_;
    ParserContext& context = conn->parser_context_;

    const size_t bytes_before_parse = buff.readable_bytes();

    // The main loop processes every available byte in the buffer.
    while (buff.readable_bytes() > 0) {
        // Look at the current character without consuming it yet.
        const char ch = *buff.data();

        switch (state) {
            case ReqLineState::START:
                if (ch == '\r' || ch == '\n') {
                    // Skip leading CRLF from some clients.
                    break;  // Character is consumed at the end of the loop.
                }

                // Check for a valid uppercase letter to start the method.
                if (ch < 'A' || ch > 'Z') {
                    return PARSE_ERROR;  // Invalid start to a request.
                }

                // First valid character found. Mark the start of the method.
                context.method_start_ = buff.data();
                state = ReqLineState::METHOD;

                // Instead of falling through, continue the loop to re-evaluate
                // the same character with the new state.
                continue;  // This skips the buff.consume(1) at the end

            case ReqLineState::METHOD:
                if (buff.data() - context.method_start_ >
                    http_limits::MAX_METHOD_LENGTH) {
                    return PARSE_ERROR;
                }

                if (ch == ' ') {
                    // Trigger: Space ends the method. Mark the end.
                    context.method_end_ = buff.data();
                    state = ReqLineState::SPACES_BEFORE_URI;
                } else if (ch < 'A' || ch > 'Z') {
                    // Invalid character within the method.
                    return PARSE_ERROR;
                }
                break;

            case ReqLineState::SPACES_BEFORE_URI:
                if (ch == ' ') {
                    // Continue skipping spaces.
                    break;  // Character is consumed at the end of the loop.
                }

                if (ch == '/') {
                    // Origin-Form
                    context.uri_start_ = buff.data();
                    context.path_start_ = buff.data();
                    state = ReqLineState::URI_PATH;
                    continue;
                } else {
                    // Invalid character after method.
                    return PARSE_ERROR;
                }

            case ReqLineState::URI_PATH:
                if (ch == ' ') {
                    // Trigger: Space ends the URI. Mark the end.
                    context.uri_end_ = buff.data();
                    context.path_end_ = buff.data();
                    state = ReqLineState::HTTP_H;
                } else if (ch == '?') {
                    // Query starts. Mark the end of the path.
                    context.path_end_ = buff.data();
                    state = ReqLineState::URI_QUERY;
                } else if (!isalnum(ch) && !strchr("/-._~:!$&'()*+,;=@", ch)) {
                    // The only other valid thing is a percent-encoding
                    if (ch == '%') {
                        context.return_state_ = state;
                        state = ReqLineState::URI_PERCENT_ENCODING_1;
                    } else {
                        return PARSE_ERROR;  // Invalid character
                    }
                }
                break;

            case ReqLineState::URI_PERCENT_ENCODING_1:
                // Expecting a hex digit after '%'
                if (isxdigit(ch)) {
                    state = ReqLineState::URI_PERCENT_ENCODING_2;
                } else {
                    return PARSE_ERROR;  // Invalid percent-encoding
                }
                break;

            case ReqLineState::URI_PERCENT_ENCODING_2:
                // Expecting a second hex digit after the first one
                if (isxdigit(ch)) {
                    // Valid percent-encoding, continue parsing the URI
                    state = context.return_state_;  // Back to last state
                } else {
                    return PARSE_ERROR;  // Invalid percent-encoding
                }
                break;

            case ReqLineState::URI_QUERY:
                if (ch == ' ') {
                    // Trigger: Space ends the URI. Mark the end.
                    context.uri_end_ = buff.data();
                    context.query_end_ = buff.data();
                    state = ReqLineState::HTTP_H;
                } else if (!isalnum(ch) && !strchr("/-._~:!$&'()*+,;=@?", ch)) {
                    // The only other valid thing is a percent-encoding
                    if (ch == '%') {
                        context.return_state_ = state;
                        state = ReqLineState::URI_PERCENT_ENCODING_1;
                    } else {
                        return PARSE_ERROR;  // Invalid character
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
                    return PARSE_ERROR;  // Invalid HTTP version start
                }
                break;

            case ReqLineState::HTTP_HT:
                if (ch == 'T') {
                    state = ReqLineState::HTTP_HTT;
                } else {
                    return PARSE_ERROR;  // Invalid HTTP version start
                }
                break;

            case ReqLineState::HTTP_HTT:
                if (ch == 'T') {
                    state = ReqLineState::HTTP_HTTP;
                } else {
                    return PARSE_ERROR;  // Invalid HTTP version start
                }
                break;

            case ReqLineState::HTTP_HTTP:
                if (ch == 'P') {
                    state = ReqLineState::HTTP_SLASH;
                } else {
                    return PARSE_ERROR;  // Invalid HTTP version start
                }
                break;

            case ReqLineState::HTTP_SLASH:
                if (ch == '/') {
                    state = ReqLineState::HTTP_MAJOR_DIGIT;
                } else {
                    return PARSE_ERROR;  // Invalid HTTP version start
                }
                break;

            case ReqLineState::HTTP_MAJOR_DIGIT:
                if (isdigit(ch)) {
                    context.version_major_ = ch - '0';
                    state = ReqLineState::HTTP_DOT;
                } else {
                    return PARSE_ERROR;  // Invalid major version digit
                }
                break;

            case ReqLineState::HTTP_DOT:
                if (ch == '.') {
                    state = ReqLineState::HTTP_MINOR_DIGIT;
                } else {
                    return PARSE_ERROR;  // Invalid character after major
                                         // version digit
                }
                break;

            case ReqLineState::HTTP_MINOR_DIGIT:
                if (isdigit(ch)) {
                    context.version_minor_ = ch - '0';
                    state = ReqLineState::VERSION_DONE;
                } else {
                    return PARSE_ERROR;  // Invalid minor version digit
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
                    ParseStatus status =
                        commit_request_line(conn->request_data_, context);

                    buff.consume(1);  // Consume the final '\n'
                    context.granular_parser_state_ = 0;  // Reset parser state

                    return status;
                }
                return PARSE_ERROR;  // Invalid character after
                                     // CR.
        }

        size_t bytes_processed_in_loop =
            bytes_before_parse - buff.readable_bytes();
        if (context.total_bytes_processed_ + bytes_processed_in_loop >
            http_limits::MAX_REQUEST_LINE_LENGTH) {
            log(LOG_WARNING, "Request line too long");
            return PARSE_REQUEST_TOO_LONG;
        }

        // Consume the character and move to the next one in the buffer
        // for the next loop iteration.
        buff.consume(1);
    }

    context.total_bytes_processed_ +=
        (bytes_before_parse - buff.readable_bytes());

    // If we exit the loop, it's because the buffer is empty. We need more
    // data.
    return PARSE_INCOMPLETE;
}

ParseStatus RequestParser::commit_request_line(HttpRequest* request,
                                               const ParserContext& context) {
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
            return PARSE_INVALID_PATH;
        }

        request->path_ = normalize_path(decoded_path);
        if (request->path_.empty()) {
            log(LOG_WARNING, "Path normalization failed for: '%s'",
                decoded_path.c_str());
            return PARSE_INVALID_PATH;
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
            return PARSE_INVALID_QUERY_STRING;
        }
    }

    log(LOG_DEBUG, "Parsed request line: %s %s %s", request->method_.c_str(),
        request->uri_.c_str(), request->version_.c_str());
    return PARSE_SUCCESS;
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

ParseStatus RequestParser::parse_headers(Connection* conn) {
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
    unsigned int& state = conn->parser_context_.granular_parser_state_;
    ParserContext& context = conn->parser_context_;

    const size_t bytes_before_parse = buff.readable_bytes();
    // The main loop processes every available byte in the buffer.
    while (buff.readable_bytes() > 0) {
        // Look at the current character without consuming it yet.
        const char ch = *buff.data();

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
                    return PARSE_ERROR;
                }

                context.key_start_ = buff.data();
                state = HeadParseState::NAME;
                continue;  // This skips the buff.consume(1) at the
                           // end of the loop

            case HeadParseState::NAME:
                if (ch == ':') {
                    if (buff.data() == context.key_start_) {
                        return PARSE_ERROR;  // Empty header name
                    }
                    context.key_end_ = buff.data();
                    state = HeadParseState::SPACE_BEFORE_VALUE;
                } else if (!is_token_char(ch)) {
                    return PARSE_ERROR;
                } else if (buff.data() - context.key_start_ >=
                           http_limits::MAX_HEADER_NAME_LENGTH) {
                    return PARSE_ERROR;
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
                context.value_start_ = buff.data();
                state = HeadParseState::VALUE;
                continue;  // Re-evaluate this character in the new VALUE state.

            case HeadParseState::VALUE:
                if (ch == '\r') {
                    context.value_end_ = buff.data();
                    state = HeadParseState::VALUE_ALMOST_DONE;
                }

                // A header value can contain any visible ASCII character,
                // spaces, and horizontal tabs. It MUST NOT contain other
                // control characters. (RFC 7230, Section 3.2)
                if (iscntrl(ch) && ch != '\t') {
                    // This correctly rejects NUL, CR, LF, and all other CTL
                    // chars.
                    return PARSE_ERROR;
                }

                if (buff.data() - context.value_start_ >
                    http_limits::MAX_HEADER_VALUE_LENGTH) {
                    return PARSE_ERROR;  // Header value too long
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
                    return PARSE_ERROR;  // Expected LF after CR
                }
                break;

            case HeadParseState::HEADERS_ALMOST_DONE:
                if (ch == '\n') {
                    // We have seen CRLF after the last header. Headers
                    // are done. Now we can determine how to handle the
                    // body.
                    buff.consume(1);  // Consume last character
                    conn->parser_context_.granular_parser_state_ =
                        0;  // Reset state
                    return PARSE_SUCCESS;
                } else {
                    return PARSE_ERROR;  // Expected LF after CR
                }
        }

        size_t bytes_processed_in_loop =
            bytes_before_parse - buff.readable_bytes();
        if (context.total_bytes_processed_ + bytes_processed_in_loop >
            http_limits::MAX_REQUEST_LINE_LENGTH) {
            log(LOG_WARNING, "Request line too long");
            return PARSE_REQUEST_TOO_LONG;
        }
        buff.consume(1);  // Consume the character
    }

    context.total_bytes_processed_ +=
        (bytes_before_parse - buff.readable_bytes());

    // If we exit the loop, it's because the buffer is empty. We need more
    // data.
    return PARSE_INCOMPLETE;
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

ParseStatus RequestParser::parse_content_body(Connection* conn) {
    log(LOG_DEBUG, "Parsing body for connection: %i", conn->client_fd_);

    HttpRequest* req = conn->request_data_;
    Buffer& buff = conn->read_buffer_;

    if (req->content_length_ == 0) {
        return PARSE_SUCCESS;
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
        req->body_.insert(req->body_.end(), buff.data(),
                          buff.data() + bytes_to_move);

        // Correctly consume only the bytes that were moved from the read
        // buffer.
        buff.consume(bytes_to_move);
        log(LOG_TRACE, "Moved %zu bytes from read buffer to request body.",
            bytes_to_move);
    }

    // Check if we have now read the entire body.
    if (req->body_.size() == req->content_length_) {
        log(LOG_DEBUG, "Body parsing complete for connection: %i",
            conn->client_fd_);
        return PARSE_SUCCESS;
    }

    // If we reach here, it means we still need more data.
    log(LOG_DEBUG,
        "Body parsing incomplete for connection: %i, need %zu more bytes.",
        conn->client_fd_, req->content_length_ - req->body_.size());
    return PARSE_INCOMPLETE;
}

// TODO ----------------------------------------------------------

// Chunked transfer encoding sends HTTP message bodies in a series of
// "chunks" without needing to know the total size in advance. Each chunk
// has a size prefix in hexadecimal notation, followed by the chunk data.
ParseStatus RequestParser::parse_chunked_body(Connection* conn) {
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
    unsigned int& state = conn->parser_context_.granular_parser_state_;
    ParserContext& context = conn->parser_context_;

    // The main loop processes every available byte in the buffer.
    while (buff.readable_bytes() > 0) {
        // Look at the current character without consuming it yet.
        const char ch = *buff.data();

        switch (state) {
            case ChunkParseState::CHUNK_START:
                if (ch == '\r') {
                    // Skip leading CRLF from some clients.
                    break;  // Character is consumed at the end of the loop.
                }

                if (!isxdigit(ch)) {
                    return PARSE_INVALID_CHUNK_SIZE;  // Invalid start
                }

                context.value_start_ = buff.data();
                state = ChunkParseState::CHUNK_SIZE;
                continue;  // This skips the buff.consume(1) at the end of
                           // the loop

            case ChunkParseState::CHUNK_SIZE:
                if (ch == ';') {
                    // Optional chunk extension starts.
                    context.value_end_ = buff.data();
                    state = ChunkParseState::CHUNK_EXTENSION;
                } else if (ch == '\r') {
                    // End of chunk size. Prepare to read data.
                    context.value_end_ = buff.data();
                    state = ChunkParseState::CHUNK_DATA;
                } else if (!isxdigit(ch)) {
                    return PARSE_INVALID_CHUNK_SIZE;  // Invalid size
                }
                break;

            case ChunkParseState::CHUNK_EXTENSION:
                if (ch == '\r') {
                    context.value_end_ = buff.data();
                    state = ChunkParseState::CHUNK_EXTENSION_ALMOST_DONE;
                }
                break;

            case ChunkParseState::CHUNK_EXTENSION_ALMOST_DONE:
                if (ch == '\n') {
                    // We have seen CRLF after the chunk size and extension.
                    state = ChunkParseState::CHUNK_DATA;
                } else {
                    return PARSE_ERROR;  // Expected LF after CR
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
                    return PARSE_ERROR;  // Expected CR after chunk data
                }
        }
    }

    // If we exit the loop, it's because the buffer is empty. We need more
    // data.
    log(LOG_DEBUG,
        "Chunked body parsing incomplete for connection: %i, need more data.",
        conn->client_fd_);
    return PARSE_INCOMPLETE;
}

ParseStatus RequestParser::parse_multipart_body(Connection* conn) {
    log(LOG_DEBUG, "Parsing multipart body for connection: %i",
        conn->client_fd_);

    if (!conn->file_upload_context_) {
        return PARSE_ERROR;
    }

    enum MultipartState {
        SEARCH_BOUNDARY,
        READ_HEADERS,
        READ_FILE_DATA,
        END_MULTIPART
    };

    Buffer& buff = conn->read_buffer_;
    FileUploadContext* upload_ctx = conn->file_upload_context_;
    std::string boundary = conn->multipart_boundary_;
    if (boundary.empty()) {
        log(LOG_ERROR, "No multipart boundary set for connection: %i",
            conn->client_fd_);
        return PARSE_ERROR;
    }
    std::string full_boundary = "--" + boundary;
    std::string end_boundary = full_boundary + "--";

    MultipartState& state = reinterpret_cast<MultipartState&>(
        conn->parser_context_.granular_parser_state_);
    static std::string headers;
    static bool file_part = false;
    static size_t file_data_start = 0;

    while (buff.readable_bytes() > 0) {
        const char* data = buff.data();
        size_t len = buff.readable_bytes();
        std::string chunk(data, len);
        size_t pos = 0;

        switch (state) {
            case SEARCH_BOUNDARY: {
                size_t bpos = chunk.find(full_boundary);
                if (bpos == std::string::npos) {
                    // Boundary not found, consume all and wait for more data
                    buff.consume(len);
                    return PARSE_INCOMPLETE;
                }
                pos = bpos + full_boundary.length();
                // There may be CRLF after the boundary
                if (chunk.substr(pos, 2) == "\r\n") pos += 2;
                buff.consume(pos);
                state = READ_HEADERS;
                headers.clear();
                file_part = false;
                break;
            }
            case READ_HEADERS: {
                // Search for end of headers (\r\n\r\n)
                std::string chunk_headers(data, len);
                size_t hpos = chunk_headers.find("\r\n\r\n");
                if (hpos == std::string::npos) {
                    // Headers incomplete, wait for more data
                    return PARSE_INCOMPLETE;
                }
                headers = chunk_headers.substr(0, hpos);
                // Check if it's a file part and extract filename
                size_t fnpos = headers.find("filename=");
                if (fnpos != std::string::npos) {
                    file_part = true;
                    // Extract filename from headers
                    size_t start = headers.find('"', fnpos);
                    size_t end = std::string::npos;
                    if (start != std::string::npos) {
                        end = headers.find('"', start + 1);
                        if (end != std::string::npos) {
                            upload_ctx->filename_ =
                                headers.substr(start + 1, end - start - 1);
                        }
                    }
                } else {
                    file_part = false;
                }
                buff.consume(hpos + 4);
                state = READ_FILE_DATA;
                file_data_start = 0;
                break;
            }
            case READ_FILE_DATA: {
                // Search for next boundary
                std::string chunk_data(data, len);
                size_t bpos = chunk_data.find(full_boundary);
                if (bpos == std::string::npos) {
                    // Boundary not found, if it's a file part, write everything
                    if (file_part) {
                        upload_ctx->upload_buffer_.append(data, len);
                    }
                    buff.consume(len);
                    return PARSE_INCOMPLETE;
                }
                // Found boundary, write until it
                if (file_part && bpos > 2) {  // Remove CRLF before the boundary
                    upload_ctx->upload_buffer_.append(data, bpos - 2);
                }
                buff.consume(bpos);
                state = SEARCH_BOUNDARY;
                // If it's the end boundary, upload complete
                if (chunk_data.substr(bpos, end_boundary.length()) ==
                    end_boundary) {
                    upload_ctx->upload_complete = true;
                    buff.consume(end_boundary.length());
                    state = END_MULTIPART;
                    return PARSE_SUCCESS;
                }
                break;
            }
            case END_MULTIPART:
                return PARSE_SUCCESS;
        }
    }
    return PARSE_INCOMPLETE;
}

std::string RequestParser::extract_boundary(const std::string& content_type) {
    size_t boundary_pos = content_type.find("boundary=");
    if (boundary_pos == std::string::npos) {
        return "";
    }
    boundary_pos += 9;  // Length of "boundary="
    if (boundary_pos >= content_type.length()) {
        return "";
    }
    if (content_type[boundary_pos] == '"') {
        boundary_pos++;
        size_t end_quote = content_type.find('"', boundary_pos);
        if (end_quote == std::string::npos) {
            return "";
        }
        return content_type.substr(boundary_pos, end_quote - boundary_pos);
    } else {
        size_t end_pos = content_type.find(";", boundary_pos);
        if (end_pos == std::string::npos) {
            end_pos = content_type.length();
        }
        return content_type.substr(boundary_pos, end_pos - boundary_pos);
    }
}
