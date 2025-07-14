#include "common.hpp"

MultipartParser::MultipartParser() {}

MultipartParser::~MultipartParser() {}

// TODO: only consume from body_buffe if we have writable space in upload buffer
ParseStatus MultipartParser::parse_multipart(Connection* conn) {
    log(LOG_TRACE, "MultipartParser::parse_multipart called for client %d",
        conn->client_fd_);

    FileUploadContext* upload_ctx = conn->file_upload_context_;
    if (!upload_ctx) {
        log(LOG_ERROR, "No upload context found for client %d",
            conn->client_fd_);
        return PARSE_ERROR;
    }

    MultipartContext& ctx = upload_ctx->multipart_context_;
    Buffer& buffer = conn->request_data_->body_buffer_;
    size_t processed = 0;

    log(LOG_DEBUG, "MultipartParser: boundary='%s', state=%d, buffer_size=%zu",
        ctx.boundary_.c_str(), ctx.state_, buffer.readable_bytes());

    while (buffer.readable_bytes() > 0) {
        const char* data = buffer.data();
        size_t len = buffer.readable_bytes();

        log(LOG_DEBUG, "Processing buffer: state=%d, len=%zu", ctx.state_, len);

        switch (ctx.state_) {
            case FIND_INITIAL_BOUNDARY: {
                // Look for the initial boundary
                std::string boundary_delimiter = "--" + ctx.boundary_;
                log(LOG_DEBUG, "Looking for boundary delimiter: '%s'",
                    boundary_delimiter.c_str());

                size_t boundary_pos =
                    std::string(data, len).find(boundary_delimiter);
                log(LOG_DEBUG, "Boundary search result: pos=%zu", boundary_pos);

                if (boundary_pos == std::string::npos) {
                    log(LOG_DEBUG,
                        "Initial boundary not found, waiting for more data");
                    return PARSE_INCOMPLETE;
                }

                // Found boundary, consume up to the end of boundary line
                size_t line_end =
                    std::string(data, len).find("\r\n", boundary_pos);
                if (line_end == std::string::npos) {
                    log(LOG_DEBUG,
                        "Boundary line incomplete, waiting for more data");
                    return PARSE_INCOMPLETE;
                }

                size_t consume_len = line_end + 2;  // Include \r\n
                buffer.consume(consume_len);
                processed += consume_len;

                ctx.state_ = READ_HEADERS;
                log(LOG_DEBUG,
                    "Found initial boundary, switching to READ_HEADERS");
                break;
            }

            case READ_HEADERS: {
                // Read headers until we find an empty line
                size_t empty_line_pos = std::string(data, len).find("\r\n\r\n");
                if (empty_line_pos == std::string::npos) {
                    log(LOG_DEBUG, "Headers incomplete, waiting for more data");
                    return PARSE_INCOMPLETE;
                }

                // Extract and parse headers
                std::string headers(data, empty_line_pos);
                parse_part_headers(headers, upload_ctx);

                // Consume headers + empty line
                size_t consume_len = empty_line_pos + 4;  // Include \r\n\r\n
                buffer.consume(consume_len);
                processed += consume_len;

                ctx.state_ = READ_DATA;
                ctx.data_start_ = buffer.data();
                ctx.data_length_ = 0;
                log(LOG_DEBUG, "Headers parsed, switching to READ_DATA");
                break;
            }

            case READ_DATA: {
                // Look for the next boundary
                std::string boundary_delimiter = "\r\n--" + ctx.boundary_;
                size_t boundary_pos =
                    std::string(data, len).find(boundary_delimiter);

                if (boundary_pos == std::string::npos) {
                    // No boundary found, all data is file content
                    if (ctx.is_file_part_) {
                        upload_ctx->upload_buffer_.append(data, len);
                        log(LOG_DEBUG,
                            "Extracted file data: %zu bytes (no boundary "
                            "found)",
                            len);
                    }
                    buffer.consume(len);
                    processed += len;
                    return PARSE_INCOMPLETE;
                }

                // Found boundary, extract file data up to boundary
                if (ctx.is_file_part_ && boundary_pos > 0) {
                    upload_ctx->upload_buffer_.append(data, boundary_pos);
                    log(LOG_DEBUG,
                        "Extracted file data: %zu bytes (boundary found)",
                        boundary_pos);
                }

                // Check if this is the final boundary
                std::string final_boundary = boundary_delimiter + "--";
                size_t final_pos =
                    std::string(data, len).find(final_boundary, boundary_pos);

                if (final_pos != std::string::npos) {
                    // Final boundary found
                    size_t consume_len = final_pos + final_boundary.length() +
                                         2;  // Include \r\n
                    buffer.consume(consume_len);
                    processed += consume_len;

                    ctx.state_ = DONE;
                    upload_ctx->upload_complete = true;
                    log(LOG_DEBUG, "Final boundary found, upload complete");
                    return PARSE_SUCCESS;
                } else {
                    // Next part boundary found
                    size_t line_end =
                        std::string(data, len).find("\r\n", boundary_pos);
                    if (line_end == std::string::npos) {
                        log(LOG_DEBUG,
                            "Boundary line incomplete, waiting for more data");
                        return PARSE_INCOMPLETE;
                    }

                    size_t consume_len = line_end + 2;  // Include \r\n
                    buffer.consume(consume_len);
                    processed += consume_len;

                    ctx.state_ = READ_HEADERS;
                    ctx.is_file_part_ = false;
                    log(LOG_DEBUG,
                        "Next boundary found, switching to READ_HEADERS");
                }
                break;
            }

            case DONE:
                log(LOG_DEBUG, "Parser already done");
                return PARSE_SUCCESS;

            default:
                log(LOG_ERROR, "Unknown multipart state: %d", ctx.state_);
                return PARSE_ERROR;
        }
    }

    log(LOG_DEBUG, "Processed %zu bytes, buffer empty", processed);
    return PARSE_INCOMPLETE;
}

void MultipartParser::commit_part_data(MultipartContext* multipart_ctx,
                                       FileUploadContext* upload_ctx) {
    if (!multipart_ctx->is_file_part_ || !multipart_ctx->data_start_ ||
        multipart_ctx->data_length_ == 0) {
        return;
    }

    // Calculate actual data length (avoid going beyond buffer)
    size_t data_len = multipart_ctx->data_length_;
    log(LOG_DEBUG, "Committing part data: %zu bytes", data_len);

    // Append to upload buffer
    upload_ctx->upload_buffer_.append(multipart_ctx->data_start_, data_len);

    // Reset for next part
    multipart_ctx->data_start_ = NULL;
    multipart_ctx->data_length_ = 0;
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
            log(LOG_DEBUG, "Detected file part with filename: '%s'",
                upload_ctx->filename_.c_str());
        } else {
            // Malformed filename attribute - treat as non-file part
            upload_ctx->multipart_context_.is_file_part_ = false;
            upload_ctx->filename_.clear();
            log(LOG_DEBUG,
                "Malformed filename attribute, treating as non-file part");
        }
    } else {
        upload_ctx->multipart_context_.is_file_part_ = false;
        upload_ctx->filename_.clear();
        log(LOG_DEBUG, "No filename found, treating as non-file part");
    }
}

std::string MultipartParser::extract_boundary(const std::string& content_type) {
    log(LOG_TRACE,
        "MultipartParser::extract_boundary called with content_type: %s",
        content_type.c_str());

    size_t boundary_pos = content_type.find("boundary=");
    if (boundary_pos == std::string::npos) {
        log(LOG_DEBUG, "No 'boundary=' found in content_type");
        return "";
    }
    boundary_pos += 9;  // Length of "boundary="
    if (boundary_pos >= content_type.length()) {
        log(LOG_DEBUG, "boundary_pos >= content_type.length()");
        return "";
    }

    std::string boundary;
    if (content_type[boundary_pos] == '\"') {
        boundary_pos++;
        size_t end_quote = content_type.find('\"', boundary_pos);
        if (end_quote == std::string::npos) {
            log(LOG_DEBUG, "No closing quote found");
            return "";
        }
        boundary = content_type.substr(boundary_pos, end_quote - boundary_pos);
    } else {
        // Find the end of the boundary (space, semicolon, or end of string)
        size_t end_pos = content_type.find_first_of(" ;", boundary_pos);
        if (end_pos == std::string::npos) {
            end_pos = content_type.length();
        }
        boundary = content_type.substr(boundary_pos, end_pos - boundary_pos);
    }

    log(LOG_DEBUG, "Extracted boundary: '%s'", boundary.c_str());

    // Add the required "--" prefix for multipart boundaries
    std::string full_boundary = "--" + boundary;
    log(LOG_DEBUG, "Full boundary with --: '%s'", full_boundary.c_str());

    return boundary;  // Return without --, they will be added when needed
}
