#include "webserv.hpp"

RequestParser::RequestParser(const ServerConfig& config) : config_(config) {}

RequestParser::~RequestParser() {}

RequestParser::ParseResult RequestParser::parse(Connection* conn) {
    // First request for this connection? Initialize
    if (!conn->request_data_) {
        conn->request_data_ = new HttpRequest();
        currentState_ = PARSING_REQUEST_LINE;
    }

    RequestParser::ParseResult result = PARSE_INCOMPLETE;

    // Process as much as possible in one call
    while (!conn->read_buffer_.empty()) {
        // Process based on current state
        switch (currentState_) {
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

        // Check the result of parsing to determine if we should continue
        if (result == PARSE_SUCCESS) {
            currentState_ = PARSING_COMPLETE;
            return PARSE_SUCCESS;
        } else if (result != PARSE_INCOMPLETE) {
            // Error occurred
            return result;
        }

        // Otherwise, continue with the next state if there's data
        // The state transition should be handled inside each parsing function
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
    std::string line(buffer.begin(), line_end);

    // Parse method, URI, and version
    size_t first_space = line.find(' ');
    size_t last_space = line.rfind(' ');

    if (first_space == std::string::npos || last_space == first_space) {
        return PARSE_ERROR;
    }

    // Extract components
    request->method_ = line.substr(0, first_space);
    request->uri_ = line.substr(first_space + 1, last_space - first_space - 1);
    request->version_ = line.substr(last_space + 1);

    // Validate method
    if (request->method_ != "GET" && request->method_ != "POST" &&
        request->method_ != "DELETE" && request->method_ != "PUT" &&
        request->method_ != "HEAD") {
        return PARSE_ERROR;
    }

    // Validate HTTP version
    if (request->version_ != "HTTP/1.0" && request->version_ != "HTTP/1.1") {
        return PARSE_ERROR;
    }

    // URI length check
    if (request->uri_.length() > HttpConfig::MAX_URI_LENGTH) {
        return PARSE_URI_TOO_LONG;
    }

    // Remove processed data (including CRLF)
    buffer.erase(buffer.begin(), line_end + 2);

    // Move to header parsing
    currentState_ = PARSING_HEADERS;
    return PARSE_INCOMPLETE;
}

RequestParser::ParseResult RequestParser::parse_headers(Connection* conn) {
    std::vector<char>& buffer = conn->read_buffer_;
    HttpRequest* request = conn->request_data_;

    while (true) {
        // Find the end of the current header line
        std::vector<char>::iterator line_end =
            std::search(buffer.begin(), buffer.end(), CRLF, &CRLF[2]);

        if (line_end == buffer.end()) {
            // Need more data
            if (buffer.size() > HttpConfig::MAX_HEADER_VALUE_LENGTH) {
                return PARSE_HEADER_TOO_LONG;
            }
            return PARSE_INCOMPLETE;
        }

        // Check for empty line (end of headers)
        if (line_end == buffer.begin()) {
            // Remove CRLF
            buffer.erase(buffer.begin(), buffer.begin() + 2);

            // Check for request body
            if (request->method_ == "POST" || request->method_ == "PUT") {
                // Check for Content-Length header
                std::string content_length =
                    request->get_header("Content-Length");
                std::string transfer_encoding =
                    request->get_header("Transfer-Encoding");

                if (!transfer_encoding.empty() &&
                    transfer_encoding.find("chunked") != std::string::npos) {
                    currentState_ = PARSING_CHUNKED_BODY;
                    return PARSE_INCOMPLETE;
                } else if (!content_length.empty()) {
                    size_t body_size =
                        std::strtoul(content_length.c_str(), NULL, 10);
                    if (body_size > HttpConfig::MAX_BODY_SIZE) {
                        return PARSE_CONTENT_TOO_LARGE;
                    }

                    if (body_size > 0) {
                        currentState_ = PARSING_BODY;
                        return PARSE_INCOMPLETE;
                    }
                }
            }

            // No body needed
            currentState_ = PARSING_COMPLETE;
            return PARSE_SUCCESS;
        }

        // Regular header line
        std::string header_line(buffer.begin(), line_end);

        // Find the colon
        size_t colon_pos = header_line.find(':');
        if (colon_pos == std::string::npos) {
            return PARSE_ERROR;
        }

        // Extract key and value
        std::string key = header_line.substr(0, colon_pos);
        std::string value = header_line.substr(colon_pos + 1);

        // Convert header name (key) to lowercase
        for (size_t i = 0; i < key.size(); ++i) {
            key[i] = std::tolower(static_cast<unsigned char>(key[i]));
        }

        // Trim whitespace
        size_t value_start = value.find_first_not_of(" \t");
        if (value_start != std::string::npos) {
            value = value.substr(value_start);
        }

        // Add header to request
        if (request->headers_.size() >= HttpConfig::MAX_HEADERS) {
            return PARSE_TOO_MANY_HEADERS;
        }

        request->headers_[key] = value;

        // Remove processed line (including CRLF)
        buffer.erase(buffer.begin(), line_end + 2);
    }
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
    currentState_ = PARSING_COMPLETE;
    return PARSE_SUCCESS;
}

RequestParser::ParseResult RequestParser::parse_chunked_body(Connection* conn) {
    std::vector<char>& buffer = conn->read_buffer_;
    HttpRequest* request = conn->request_data_;

    // Temporary storage for assembled body
    if (!request->temp_body_) {
        request->temp_body_ = new std::vector<char>();
    }

    while (true) {
        // We're either at the start of a chunk or continuing an existing chunk
        if (chunkRemaining_ == 0) {
            // Need to read a new chunk size
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

            // Parse hex value
            char* end_ptr;
            chunkRemaining_ =
                std::strtoul(chunk_size_line.c_str(), &end_ptr, 16);

            // Ensure valid hex number
            if (end_ptr == chunk_size_line.c_str() ||
                (*end_ptr != '\0' && *end_ptr != ' ' && *end_ptr != '\t')) {
                return PARSE_INVALID_CHUNK_SIZE;
            }

            // Remove chunk size line
            buffer.erase(buffer.begin(), line_end + 2);

            // If chunk size is 0, this is the last chunk
            if (chunkRemaining_ == 0) {
                // Look for the final CRLF
                std::vector<char>::iterator final_crlf =
                    std::search(buffer.begin(), buffer.end(), CRLF, &CRLF[2]);

                if (final_crlf == buffer.end()) {
                    return PARSE_INCOMPLETE;
                }

                // Remove final CRLF
                buffer.erase(buffer.begin(), final_crlf + 2);

                // Copy temp body to final body
                request->body_.assign(request->temp_body_->begin(),
                                      request->temp_body_->end());
                delete request->temp_body_;
                request->temp_body_ = NULL;

                currentState_ = PARSING_COMPLETE;
                return PARSE_SUCCESS;
            }

            // Continue to read chunk data
            continue;
        }

        // We're in the middle of a chunk
        size_t bytes_to_read = std::min(chunkRemaining_, buffer.size());
        if (bytes_to_read == 0) {
            return PARSE_INCOMPLETE;
        }

        // Append to temporary body
        request->temp_body_->insert(request->temp_body_->end(), buffer.begin(),
                                    buffer.begin() + bytes_to_read);

        // Remove processed data
        buffer.erase(buffer.begin(), buffer.begin() + bytes_to_read);
        chunkRemaining_ -= bytes_to_read;

        // If we've completed this chunk, look for the trailing CRLF
        if (chunkRemaining_ == 0) {
            if (buffer.size() < 2) {
                return PARSE_INCOMPLETE;
            }

            // Verify and consume CRLF
            if (buffer[0] != '\r' || buffer[1] != '\n') {
                return PARSE_ERROR;
            }

            buffer.erase(buffer.begin(), buffer.begin() + 2);
        }
    }
}
