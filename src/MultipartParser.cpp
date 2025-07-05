#include "common.hpp"

MultipartParser::MultipartParser() {}

MultipartParser::~MultipartParser() {}

ParseStatus MultipartParser::parse_multipart(Connection* conn) {
    log(LOG_DEBUG, "Parsing multipart body for connection: %i", conn->client_fd_);

    if (!conn->file_upload_context_) {
        log(LOG_FATAL, "MultipartParser: No file upload context for connection: %i", conn->client_fd_);
        return PARSE_ERROR;
    }

    FileUploadContext* upload_ctx = conn->file_upload_context_;
    MultipartContext* multipart_ctx = &upload_ctx->multipart_context_;
    MultipartState& state = multipart_ctx->state_;
    Buffer& buff = conn->request_data_->body_data_;

    // Pre-build the boundary markers we'll be matching against
    const std::string initial_boundary = "--" + multipart_ctx->boundary_;
    const std::string subsequent_boundary = "\r\n--" + multipart_ctx->boundary_;

    while (buff.readable_bytes() > 0) {
        const char ch = buff.peek();

        switch (state) {
            case SEARCH_INITIAL_BOUNDARY: {
                // Match the first boundary: "--boundary"
                if (ch == initial_boundary[multipart_ctx->boundary_match_index_]) {
                    multipart_ctx->boundary_match_index_++;
                    if (multipart_ctx->boundary_match_index_ == initial_boundary.length()) {
                        state = VALIDATE_FINAL_BOUNDARY; // Check for -- or \r\n
                        multipart_ctx->boundary_match_index_ = 0;
                    }
                } else {
                    // Not a boundary, this is an error in strict parsing
                    return PARSE_ERROR;
                }
                break;
            }

            case READ_PART_HEADERS: {
                // Accumulate headers until we see "\r\n\r\n"
                multipart_ctx->part_headers_ += ch;
                if (multipart_ctx->part_headers_.length() > 4 &&
                    multipart_ctx->part_headers_.substr(multipart_ctx->part_headers_.length() - 4) == "\r\n\r\n") {
                    
                    // Headers complete, process them
                    std::string& headers = multipart_ctx->part_headers_;
                    headers.resize(headers.length() - 4); // Trim the final \r\n\r\n

                    if (headers.find("filename=\"") != std::string::npos) {
                        multipart_ctx->is_file_part_ = true;
                        // (Your existing logic to extract filename)
                        size_t start = headers.find("filename=\"") + 10;
                        size_t end = headers.find('"', start);
                        if (end != std::string::npos) {
                            upload_ctx->filename_ = headers.substr(start, end - start);
                        }
                    } else {
                        multipart_ctx->is_file_part_ = false;
                    }
                    
                    multipart_ctx->part_headers_.clear();
                    state = ACCUMULATE_PART_DATA;
                }
                break;
            }

            case ACCUMULATE_PART_DATA: {
                // We are in the body of a part. We append data but must
                // also check for the start of the next boundary.
                if (ch == subsequent_boundary[0]) { // Potential start ('\r')
                    state = MATCH_BOUNDARY;
                    multipart_ctx->boundary_match_index_ = 1; // We've matched one char
                } else {
                    if (multipart_ctx->is_file_part_) {
                        upload_ctx->upload_buffer_.append(&ch, 1);
                    }
                }
                break;
            }

            case MATCH_BOUNDARY: {
                if (ch == subsequent_boundary[multipart_ctx->boundary_match_index_]) {
                    // Character continues to match the boundary
                    multipart_ctx->boundary_match_index_++;
                    if (multipart_ctx->boundary_match_index_ == subsequent_boundary.length()) {
                        // Full boundary matched!
                        state = VALIDATE_FINAL_BOUNDARY;
                        multipart_ctx->boundary_match_index_ = 0;
                    }
                } else {
                    // Match failed. This was a false alarm.
                    // We must write the partial match data we held back.
                    if (multipart_ctx->is_file_part_) {
                        upload_ctx->upload_buffer_.append(subsequent_boundary.c_str(), multipart_ctx->boundary_match_index_);
                        upload_ctx->upload_buffer_.append(&ch, 1); // And the current char
                    }
                    // Reset and go back to accumulating data
                    multipart_ctx->boundary_match_index_ = 0;
                    state = ACCUMULATE_PART_DATA;
                }
                break;
            }

            case VALIDATE_FINAL_BOUNDARY: {
                if (ch == '-') {
                    // Potentially the final boundary: "--boundary--"
                    state = END_MULTIPART; // Assume final, next char must be '-'
                } else if (ch == '\r') {
                    // A new part is starting: "--boundary\r\n"
                    state = READ_PART_HEADERS; // Next char must be '\n'
                } else {
                    return PARSE_ERROR; // Malformed boundary
                }
                break;
            }

            case END_MULTIPART: {
                if (ch == '-') { // Confirmed final boundary
                    upload_ctx->upload_complete = true;
                    conn->request_data_->body_fully_parsed_ = true;
                    buff.consume(1); // Consume final '-'
                    return PARSE_SUCCESS;
                }
                return PARSE_ERROR; // Malformed final boundary
            }
            
            case MULTIPART_ERROR:
                return PARSE_ERROR;
        }
        buff.consume(1);
    }

    return PARSE_INCOMPLETE;
}

std::string MultipartParser::extract_boundary(const std::string& content_type) {
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