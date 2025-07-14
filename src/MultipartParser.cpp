#include "common.hpp"

MultipartParser::MultipartParser() {}

MultipartParser::~MultipartParser() {}

ParseStatus MultipartParser::parse_multipart(Connection* conn) {
    if (!conn->file_upload_context_) {
        log(LOG_FATAL,
            "MultipartParser: No file upload context for connection: %d",
            conn->client_fd_);
        return PARSE_ERROR;
    }

    log(LOG_TRACE, "MultipartParser::parse_multipart called for client %d",
        conn->client_fd_);

    FileUploadContext* upload_ctx = conn->file_upload_context_;
    MultipartContext* multipart_ctx = &upload_ctx->multipart_context_;
    MultipartState& state = multipart_ctx->state_;
    Buffer& buff = conn->request_data_->body_buffer_;

    const std::string initial_boundary = "--" + multipart_ctx->boundary_;
    const std::string subsequent_boundary = "\r\n--" + multipart_ctx->boundary_;

    while (buff.readable_bytes() > 0) {
        const char ch = buff.peek();

        switch (state) {
            case FIND_INITIAL_BOUNDARY: {
                if (ch ==
                    initial_boundary[multipart_ctx->boundary_match_index_]) {
                    multipart_ctx->boundary_match_index_++;
                    if (multipart_ctx->boundary_match_index_ ==
                        initial_boundary.length()) {
                        multipart_ctx->boundary_match_index_ = 0;
                        state = CHECK_BOUNDARY_TYPE;
                    }
                } else {
                    log(LOG_ERROR,
                        "Initial boundary not found at start of multipart "
                        "body");
                    return PARSE_ERROR;
                }
                break;
            }

            case READ_HEADERS: {
                // Simple end-of-headers detection: look for \r\n\r\n
                static const char* eoh_pattern = "\r\n\r\n";
                if (ch == eoh_pattern[multipart_ctx->boundary_match_index_]) {
                    multipart_ctx->boundary_match_index_++;
                    if (multipart_ctx->boundary_match_index_ == 4) {
                        // Found end of headers
                        parse_part_headers(multipart_ctx->part_headers_,
                                           upload_ctx);
                        multipart_ctx->part_headers_.clear();
                        multipart_ctx->boundary_match_index_ = 0;
                        state = READ_DATA;
                    }
                } else {
                    // Reset match index and add all the characters we thought
                    // were matching
                    for (size_t i = 0; i < multipart_ctx->boundary_match_index_;
                         ++i) {
                        multipart_ctx->part_headers_ += eoh_pattern[i];
                    }
                    multipart_ctx->part_headers_ += ch;
                    multipart_ctx->boundary_match_index_ = 0;

                    // Check for header size limit
                    if (multipart_ctx->part_headers_.length() >
                        http_limits::MAX_REQUEST_HEAD_LENGTH) {
                        log(LOG_WARNING, "Multipart part header is too long");
                        return PARSE_ERROR;
                    }
                }
                break;
            }
            case READ_DATA: {
                // The boundary always starts with '\r'
                if (ch == '\r') {
                    multipart_ctx->boundary_match_index_ = 1;
                    state = POTENTIAL_BOUNDARY;
                } else {
                    if (multipart_ctx->is_file_part_) {
                        upload_ctx->upload_buffer_.append(&ch, 1);
                    }
                }
                break;
            }

            case POTENTIAL_BOUNDARY: {
                if (ch ==
                    subsequent_boundary[multipart_ctx->boundary_match_index_]) {
                    multipart_ctx->boundary_match_index_++;
                    if (multipart_ctx->boundary_match_index_ ==
                        subsequent_boundary.length()) {
                        multipart_ctx->boundary_match_index_ = 0;
                        state = CHECK_BOUNDARY_TYPE;
                    }
                } else {
                    // It was a false alarm. The buffered characters are data.
                    if (multipart_ctx->is_file_part_) {
                        size_t bytes_to_append =
                            multipart_ctx->boundary_match_index_;
                        size_t bytes_appended =
                            upload_ctx->upload_buffer_.append(
                                subsequent_boundary.c_str(), bytes_to_append);

                        if (bytes_appended < bytes_to_append) {
                            // Buffer is full, couldn't append all data.
                            // We need to stop and wait for the buffer to be
                            // written.
                            return PARSE_INCOMPLETE;
                        }

                        bytes_appended =
                            upload_ctx->upload_buffer_.append(&ch, 1);
                        if (bytes_appended < 1) {
                            // Buffer is full, couldn't append the final char.
                            return PARSE_INCOMPLETE;
                        }
                    }
                    multipart_ctx->boundary_match_index_ = 0;
                    state = READ_DATA;
                }
                break;
            }

            case CHECK_BOUNDARY_TYPE: {
                if (ch == '-') {
                    state = EXPECT_FINAL_DASH;
                } else if (ch == '\r') {
                    state = EXPECT_NEWLINE;
                } else {
                    log(LOG_ERROR, "Invalid character after boundary");
                    return PARSE_ERROR;
                }
                break;
            }

            case EXPECT_NEWLINE: {
                if (ch == '\n') {
                    state = READ_HEADERS;
                } else {
                    log(LOG_ERROR, "Expected \\n after \\r in boundary");
                    return PARSE_ERROR;
                }
                break;
            }

            case EXPECT_FINAL_DASH: {
                if (ch == '-') {
                    upload_ctx->upload_complete = true;
                    state = EXPECT_TRAILING_CR;
                } else {
                    log(LOG_ERROR, "Expected second '-' in final boundary");
                    return PARSE_ERROR;
                }
                break;
            }

            case EXPECT_TRAILING_CR: {
                if (ch == '\r') {
                    state = EXPECT_TRAILING_LF;
                } else {
                    // Trailing CR is optional, so we can treat it as a normal
                    // character
                    log(LOG_INFO,
                        "Successfully parsed multipart chunk for client %d",
                        conn->client_fd_);
                    buff.consume(1);
                    return PARSE_SUCCESS;
                }
                break;
            }

            case EXPECT_TRAILING_LF: {
                if (ch == '\n') {
                    log(LOG_INFO,
                        "Successfully parsed multipart chunk for client %d",
                        conn->client_fd_);
                    buff.consume(1);
                    return PARSE_SUCCESS;
                } else {
                    log(LOG_ERROR, "Expected \\n after \\r in final boundary");
                    return PARSE_ERROR;
                }
                break;
            }

            default: {
                log(LOG_FATAL, "Unknown multipart parser state");
                return PARSE_ERROR;
            }
        }

        buff.consume(1);
    }

    log(LOG_TRACE,
        "MultipartParser::parse_multipart: Incomplete parsing for client %d",
        conn->client_fd_);
    return PARSE_INCOMPLETE;
}

void MultipartParser::parse_part_headers(const std::string& headers,
                                         FileUploadContext* upload_ctx) {
    log(LOG_TRACE, "MultipartParser::parse_part_headers called for fd %d",
        upload_ctx->file_fd_);

    size_t filename_pos = headers.find("filename=\"");
    if (filename_pos != std::string::npos) {
        upload_ctx->multipart_context_.is_file_part_ = true;
        size_t start = filename_pos + 10;
        size_t end = headers.find('"', start);
        if (end != std::string::npos) {
            upload_ctx->filename_ = headers.substr(start, end - start);
        } else {
            // Malformed filename attribute - treat as non-file part
            upload_ctx->multipart_context_.is_file_part_ = false;
            upload_ctx->filename_.clear();
        }
    } else {
        upload_ctx->multipart_context_.is_file_part_ = false;
        upload_ctx->filename_.clear();
    }
}

std::string MultipartParser::extract_boundary(const std::string& content_type) {
    log(LOG_TRACE,
        "MultipartParser::extract_boundary called with content_type: %s",
        content_type.c_str());

    size_t boundary_pos = content_type.find("boundary=");
    if (boundary_pos == std::string::npos) {
        return "";
    }
    boundary_pos += 9;  // Length of "boundary="
    if (boundary_pos >= content_type.length()) {
        return "";
    }
    std::string boundary;
    if (content_type[boundary_pos] == '"') {
        boundary_pos++;
        size_t end_quote = content_type.find('"', boundary_pos);
        if (end_quote == std::string::npos) {
            return "";
        }
        boundary = content_type.substr(boundary_pos, end_quote - boundary_pos);
    } else {
        size_t end_pos = content_type.find(";", boundary_pos);
        if (end_pos == std::string::npos) {
            end_pos = content_type.length();
        }
        boundary = content_type.substr(boundary_pos, end_pos - boundary_pos);
    }

    return boundary;
}
