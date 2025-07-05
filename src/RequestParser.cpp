#include "common.hpp"

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
        const char ch = buff.peek();

        switch (state) {
            case START:
                if (ch == '\r' || ch == '\n') {
                    // Skip leading CRLF from some clients.
                    break;
                }

                if (ch < 'A' || ch > 'Z') {
                    return PARSE_ERROR;
                }

                context.method_start_ = buff.data();
                state = METHOD;

                // Instead of falling through, continue the loop to re-evaluate
                // the same character with the new state.
                continue;

            case METHOD:
                if (static_cast<size_t>(buff.data() - context.method_start_) >
                    http_limits::MAX_METHOD_LENGTH) {
                    return PARSE_ERROR;
                }

                if (ch == ' ') {
                    context.method_end_ = buff.data();
                    state = SPACES_BEFORE_URI;
                } else if (ch < 'A' || ch > 'Z') {
                    return PARSE_ERROR;
                }
                break;

            case SPACES_BEFORE_URI:
                if (ch == ' ') {
                    break;
                }

                if (ch == '/') {
                    // Only Origin-Form accepted
                    context.uri_start_ = buff.data();
                    context.path_start_ = buff.data();
                    state = URI_PATH;
                    continue;
                } else {
                    return PARSE_ERROR;
                }

            case URI_PATH:
                if (ch == ' ') {
                    context.uri_end_ = buff.data();
                    context.path_end_ = buff.data();
                    state = HTTP_H;
                } else if (ch == '?') {
                    context.path_end_ = buff.data();
                    state = URI_QUERY;
                } else if (!isalnum(ch) && !strchr("/-._~:!$&'()*+,;=@", ch)) {
                    if (ch == '%') {
                        context.return_state_ = state;
                        state = URI_PERCENT_ENCODING_1;
                    } else {
                        return PARSE_ERROR;
                    }
                }
                break;

            case URI_PERCENT_ENCODING_1:
                if (isxdigit(ch)) {
                    state = URI_PERCENT_ENCODING_2;
                } else {
                    return PARSE_ERROR;
                }
                break;

            case URI_PERCENT_ENCODING_2:
                if (isxdigit(ch)) {
                    state = context.return_state_;
                } else {
                    return PARSE_ERROR;
                }
                break;

            case URI_QUERY:
                if (ch == ' ') {
                    context.uri_end_ = buff.data();
                    context.query_end_ = buff.data();
                    state = HTTP_H;
                } else if (!isalnum(ch) && !strchr("/-._~:!$&'()*+,;=@?", ch)) {
                    if (ch == '%') {
                        context.return_state_ = state;
                        state = URI_PERCENT_ENCODING_1;
                    } else {
                        return PARSE_ERROR;
                    }
                }
                break;

            case SPACES_AFTER_URI:
                if (ch == ' ') {
                    break;
                } else {
                    state = HTTP_H;
                }
                break;

            case HTTP_H:
                if (ch == 'H') {
                    state = HTTP_HT;
                } else {
                    return PARSE_ERROR;
                }
                break;

            case HTTP_HT:
                if (ch == 'T') {
                    state = HTTP_HTT;
                } else {
                    return PARSE_ERROR;
                }
                break;

            case HTTP_HTT:
                if (ch == 'T') {
                    state = HTTP_HTTP;
                } else {
                    return PARSE_ERROR;
                }
                break;

            case HTTP_HTTP:
                if (ch == 'P') {
                    state = HTTP_SLASH;
                } else {
                    return PARSE_ERROR;
                }
                break;

            case HTTP_SLASH:
                if (ch == '/') {
                    state = HTTP_MAJOR_DIGIT;
                } else {
                    return PARSE_ERROR;
                }
                break;

            case HTTP_MAJOR_DIGIT:
                if (isdigit(ch)) {
                    context.version_major_ = ch - '0';
                    state = HTTP_DOT;
                } else {
                    return PARSE_ERROR;
                }
                break;

            case HTTP_DOT:
                if (ch == '.') {
                    state = HTTP_MINOR_DIGIT;
                } else {
                    return PARSE_ERROR;
                }
                break;

            case HTTP_MINOR_DIGIT:
                if (isdigit(ch)) {
                    context.version_minor_ = ch - '0';
                    state = VERSION_DONE;
                } else {
                    return PARSE_ERROR;
                }
                break;

            case VERSION_DONE:
                if (ch == '\r') {
                    state = ALMOST_DONE;
                }
                break;

            case ALMOST_DONE:
                if (ch == '\n') {
                    ParseStatus status =
                        commit_request_line(conn->request_data_, context);

                    buff.consume(1);
                    context.clear_for_next_state();

                    return status;
                }
                return PARSE_ERROR;
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
    decoded_path.reserve(path.length());

    for (size_t i = 0; i < path.length(); i++) {
        if (path[i] == '%') {
            if (i + 2 >= path.length()) {
                return "";
            }

            char hex1 = path[i + 1];
            char hex2 = path[i + 2];

            int value = (hex_to_int(hex1) << 4) | hex_to_int(hex2);

            if (value < ' ' || value > '~') {
                return "";
            }

            // Security: Reject double-encoded attempts
            if (value == '%') {
                return "";
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
        return "";
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
    decoded_query.reserve(query.length());

    for (size_t i = 0; i < query.length(); i++) {
        if (query[i] == '%') {
            if (i + 2 >= query.length()) {
                return "";
            }

            char hex1 = query[i + 1];
            char hex2 = query[i + 2];

            int value = (hex_to_int(hex1) << 4) | hex_to_int(hex2);

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

    while (buff.readable_bytes() > 0) {
        const char ch = buff.peek();

        switch (state) {
            case LINE_START:
                if (ch == '\r') {
                    state = HEADERS_ALMOST_DONE;
                    break;
                }

                if (!is_token_char(ch)) {
                    return PARSE_ERROR;
                }

                context.key_start_ = buff.data();
                state = NAME;
                // Skip the buff.consume(1) at the end of the loop
                continue;

            case NAME:
                if (ch == ':') {
                    if (buff.data() == context.key_start_) {
                        return PARSE_ERROR;
                    }
                    context.key_end_ = buff.data();
                    state = SPACE_BEFORE_VALUE;
                } else if (!is_token_char(ch)) {
                    return PARSE_ERROR;
                } else if (static_cast<size_t>(buff.data() -
                                               context.key_start_) >=
                           http_limits::MAX_HEADER_NAME_LENGTH) {
                    return PARSE_ERROR;
                }
                break;

            case SPACE_BEFORE_VALUE:
                if (ch == ' ' || ch == '\t') {
                    break;
                }

                if (ch == '\r') {
                    context.value_start_ = NULL;
                    context.value_end_ = NULL;
                    state = VALUE_ALMOST_DONE;
                    break;
                }

                context.value_start_ = buff.data();
                state = VALUE;
                continue;  // Re-evaluate this character in the new VALUE state.

            case VALUE:
                if (ch == '\r') {
                    context.value_end_ = buff.data();
                    state = VALUE_ALMOST_DONE;
                    break;
                }

                // A header value can contain any visible ASCII character,
                // spaces, and horizontal tabs. It MUST NOT contain other
                // control characters. (RFC 7230, Section 3.2)
                if (iscntrl(ch) && ch != '\t') {
                    return PARSE_ERROR;
                }

                if (static_cast<size_t>(buff.data() - context.value_start_) >
                    http_limits::MAX_HEADER_VALUE_LENGTH) {
                    return PARSE_ERROR;
                }
                break;

            case VALUE_ALMOST_DONE:
                if (ch == '\n') {
                    commit_header(conn->request_data_, context);
                    context.key_start_ = NULL;
                    context.key_end_ = NULL;
                    context.value_start_ = NULL;
                    context.value_end_ = NULL;
                    state = LINE_START;
                } else {
                    return PARSE_ERROR;
                }
                break;

            case HEADERS_ALMOST_DONE:
                if (ch == '\n') {
                    buff.consume(1);
                    context.clear_for_next_state();
                    return PARSE_SUCCESS;
                } else {
                    return PARSE_ERROR;
                }
        }

        size_t bytes_processed_in_loop =
            bytes_before_parse - buff.readable_bytes();
        if (context.total_bytes_processed_ + bytes_processed_in_loop >
            http_limits::MAX_REQUEST_LINE_LENGTH) {
            log(LOG_WARNING, "Request line too long");
            return PARSE_REQUEST_TOO_LONG;
        }
        // Consume the character and move to the next one in the buffer
        buff.consume(1);
    }

    context.total_bytes_processed_ +=
        (bytes_before_parse - buff.readable_bytes());

    // If we exit the loop, it's because the buffer is empty. We need more
    // data.
    return PARSE_INCOMPLETE;
}

// It checks if a character is a valid "tchar" according to RFC 7230.
bool RequestParser::is_token_char(char c) {
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
    size_t& body_remaining_bytes = conn->parser_context_.body_remaining_bytes_;

    if (req->content_length_ == 0) {
        return PARSE_SUCCESS;
    }

    size_t unloaded_bytes =
        buff.unload_to(req->body_buffer_, body_remaining_bytes);
    body_remaining_bytes -= unloaded_bytes;

    log(LOG_DEBUG, "Unloaded %zu bytes to body buffer for connection: %i",
        unloaded_bytes, conn->client_fd_);

    if (body_remaining_bytes == 0) {
        req->body_fully_parsed_ = true;
        log(LOG_DEBUG, "Body parsing complete for connection: %i",
            conn->client_fd_);
        return PARSE_SUCCESS;
    }

    log(LOG_DEBUG,
        "Body parsing incomplete for connection: %i, need %zu more bytes.",
        conn->client_fd_,
        req->content_length_ - req->body_buffer_.readable_bytes());
    return PARSE_INCOMPLETE;
}

// Chunked transfer encoding sends HTTP message bodies in a series of
// "chunks" without needing to know the total size in advance. Each chunk
// has a size prefix in hexadecimal notation, followed by the chunk data.
ParseStatus RequestParser::parse_chunked_body(Connection* conn) {
    log(LOG_DEBUG, "Parsing chunked body for connection: %i", conn->client_fd_);

    enum ChunkParseState {
        CHUNK_SIZE_START,
        CHUNK_SIZE,
        CHUNK_SIZE_ALMOST_DONE,
        CHUNK_EXTENSION,
        CHUNK_EXTENSION_ALMOST_DONE,
        CHUNK_DATA,
        AFTER_DATA,
        AFTER_DATA_ALMOST_DONE,
        LAST_CHUNK,
        LAST_CHUNK_ALMOST_DONE,
        CHECK_TRAILER_LINE_START,
        TRAILER_LINE_CONSUME,
        TRAILER_LINE_ALMOST_DONE,
        BODY_ALMOST_DONE
    };

    Buffer& buff = conn->read_buffer_;
    unsigned int& state = conn->parser_context_.granular_parser_state_;
    ParserContext& context = conn->parser_context_;

    while (buff.readable_bytes() > 0) {
        const char ch = buff.peek();

        switch (state) {
            case CHUNK_SIZE_START:
                if (!isxdigit(ch)) {
                    log(LOG_ERROR, "Invalid character in chunk size: '%c'", ch);
                    return PARSE_ERROR;
                }
                context.chunk_remaining_bytes_ = hex_to_int(ch);
                state = CHUNK_SIZE;
                break;

            case CHUNK_SIZE:
                if (ch == '\r') {
                    state = CHUNK_SIZE_ALMOST_DONE;
                } else if (ch == ';') {
                    state = CHUNK_EXTENSION;
                } else if (isxdigit(ch)) {
                    context.chunk_remaining_bytes_ =
                        (context.chunk_remaining_bytes_ << 4) + hex_to_int(ch);
                } else {
                    log(LOG_ERROR, "Invalid character in chunk size: '%c'", ch);
                    return PARSE_ERROR;
                }
                break;

            case CHUNK_SIZE_ALMOST_DONE:
                if (ch == '\n') {
                    if (context.chunk_remaining_bytes_ == 0) {
                        state = LAST_CHUNK;
                    } else {
                        state = CHUNK_DATA;
                    }
                } else {
                    log(LOG_ERROR, "Expected LF after CR in chunk size.");
                    return PARSE_ERROR;
                }
                break;

            case CHUNK_EXTENSION:
                if (ch == '\r') {
                    state = CHUNK_EXTENSION_ALMOST_DONE;
                }
                break;

            case CHUNK_EXTENSION_ALMOST_DONE:
                if (ch == '\n') {
                    if (context.chunk_remaining_bytes_ == 0) {
                        state = LAST_CHUNK;
                    } else {
                        state = CHUNK_DATA;
                    }
                } else {
                    log(LOG_ERROR, "Expected LF after CR in chunk extension.");
                    return PARSE_ERROR;
                }
                break;

            case CHUNK_DATA: {
                size_t bytes_to_process = std::min(
                    buff.readable_bytes(), context.chunk_remaining_bytes_);

                if (bytes_to_process > 0) {
                    size_t bytes_unloaded = buff.unload_to(
                        conn->request_data_->body_buffer_, bytes_to_process);
                    context.chunk_remaining_bytes_ -= bytes_unloaded;
                }

                if (context.chunk_remaining_bytes_ == 0) {
                    state = AFTER_DATA;
                }
                // Skip the buff.consume(1) at the end of the loop as we already
                // consumed bytes above.
                continue;
            }

            case AFTER_DATA:
                if (ch == '\r') {
                    state = AFTER_DATA_ALMOST_DONE;
                } else {
                    log(LOG_ERROR, "Expected CR after chunk data, got '%c'",
                        ch);
                    return PARSE_ERROR;
                }
                break;

            case AFTER_DATA_ALMOST_DONE:
                if (ch == '\n') {
                    state = CHUNK_SIZE_START;
                } else {
                    log(LOG_ERROR,
                        "Expected LF after CR in chunk data, got '%c'", ch);
                    return PARSE_ERROR;
                }
                break;

            case LAST_CHUNK:
                if (ch == '\r') {
                    state = LAST_CHUNK_ALMOST_DONE;
                } else {
                    log(LOG_ERROR, "Expected CR before last chunk, got '%c'",
                        ch);
                    return PARSE_ERROR;
                }
                break;

            case LAST_CHUNK_ALMOST_DONE:
                if (ch == '\n') {
                    state = CHECK_TRAILER_LINE_START;
                } else {
                    log(LOG_ERROR,
                        "Expected LF after CR in last chunk, got '%c'", ch);
                    return PARSE_ERROR;
                }
                break;

            case CHECK_TRAILER_LINE_START:
                if (ch == '\r') {
                    state = BODY_ALMOST_DONE;
                } else {
                    state = TRAILER_LINE_CONSUME;
                }
                break;

            case TRAILER_LINE_CONSUME:
                if (ch == '\r') {
                    state = TRAILER_LINE_ALMOST_DONE;
                }
                break;

            case TRAILER_LINE_ALMOST_DONE:
                if (ch == '\n') {
                    state = CHECK_TRAILER_LINE_START;
                } else {
                    log(LOG_ERROR, "Invalid character in trailer line: '%c'",
                        ch);
                    return PARSE_ERROR;
                }
                break;

            case BODY_ALMOST_DONE:
                if (ch == '\n') {
                    buff.consume(1);
                    context.clear_for_next_state();
                    conn->request_data_->body_fully_parsed_ = true;
                    log(LOG_DEBUG,
                        "Chunked body parsing complete for connection: "
                        "%i",
                        conn->client_fd_);
                    return PARSE_SUCCESS;
                } else {
                    log(LOG_ERROR, "Expected LF after CR in chunked body.");
                    return PARSE_ERROR;
                }
                break;
        }

        // Consume the character and move to the next one in the buffer
        buff.consume(1);
    }

    // If we exit the loop, it's because the buffer is empty. We need more
    // data.
    log(LOG_DEBUG,
        "Chunked body parsing incomplete for connection: %i, need more "
        "data.",
        conn->client_fd_);
    return PARSE_INCOMPLETE;
}
