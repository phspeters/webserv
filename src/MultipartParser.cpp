#include "common.hpp"

MultipartParser::MultipartParser() {}

MultipartParser::~MultipartParser() {}

ParseStatus MultipartParser::parse_multipart(Connection* conn) {
    log(LOG_DEBUG, "Parsing multipart body for connection: %i",
        conn->client_fd_);

    if (!conn->file_upload_context_) {
        log(LOG_FATAL,
            "MultipartParser: No file upload context for connection: %i",
            conn->client_fd_);
        return PARSE_ERROR;
    }

    FileUploadContext* upload_ctx = conn->file_upload_context_;
    MultipartContext* multipart_ctx = &upload_ctx->multipart_context_;
    MultipartState& state = multipart_ctx->state_;
    Buffer& buff = conn->request_data_->body_buffer_;

    const std::string initial_boundary = "--" + multipart_ctx->boundary_;
    const std::string subsequent_boundary = "\r\n--" + multipart_ctx->boundary_;

    while (buff.readable_bytes() > 0) {
        if (state == ACCUMULATE_PART_DATA) {
            const char* data_start = buff.data();

            const char* boundary_start = static_cast<const char*>(memchr(
                data_start, subsequent_boundary[0], buff.readable_bytes()));

            if (boundary_start) {
                size_t data_len = boundary_start - data_start;
                if (multipart_ctx->is_file_part_ && data_len > 0) {
                    upload_ctx->upload_buffer_.append(data_start, data_len);
                }

                buff.consume(data_len);

                state = MATCH_BOUNDARY;
                continue;
            } else {
                size_t safe_len = 0;
                if (buff.readable_bytes() > subsequent_boundary.length()) {
                    safe_len =
                        buff.readable_bytes() - subsequent_boundary.length();
                }

                if (multipart_ctx->is_file_part_ && safe_len > 0) {
                    upload_ctx->upload_buffer_.append(data_start, safe_len);
                    buff.consume(safe_len);
                }

                return PARSE_INCOMPLETE;
            }
        }

        const char ch = buff.peek();

        switch (state) {
            case SEARCH_INITIAL_BOUNDARY:
                if (ch ==
                    initial_boundary[multipart_ctx->boundary_match_index_]) {
                    multipart_ctx->boundary_match_index_++;
                    if (multipart_ctx->boundary_match_index_ ==
                        initial_boundary.length()) {
                        state = BOUNDARY_ALMOST_DONE;
                        multipart_ctx->boundary_match_index_ = 0;
                    }
                } else {
                    log(LOG_ERROR,
                        "Multipart body did not start with initial boundary.");
                    return PARSE_ERROR;
                }
                break;

            case READ_PART_HEADERS:
                multipart_ctx->part_headers_ += ch;
                if (multipart_ctx->part_headers_.length() >= 4 &&
                    multipart_ctx->part_headers_.substr(
                        multipart_ctx->part_headers_.length() - 4) ==
                        "\r\n\r\n") {
                    std::string& headers = multipart_ctx->part_headers_;
                    headers.resize(headers.length() - 4);

                    size_t filename_pos = headers.find("filename=\"");
                    if (filename_pos != std::string::npos) {
                        multipart_ctx->is_file_part_ = true;
                        size_t start =
                            filename_pos + 10;  // length of "filename=\""
                        size_t end = headers.find('"', start);
                        if (end != std::string::npos) {
                            upload_ctx->filename_ =
                                headers.substr(start, end - start);
                        }
                    } else {
                        multipart_ctx->is_file_part_ = false;
                    }

                    multipart_ctx->part_headers_.clear();
                    state = ACCUMULATE_PART_DATA;
                }
                break;

            case MATCH_BOUNDARY:
                if (ch ==
                    subsequent_boundary[multipart_ctx->boundary_match_index_]) {
                    multipart_ctx->boundary_match_index_++;
                    if (multipart_ctx->boundary_match_index_ ==
                        subsequent_boundary.length()) {
                        state = VALIDATE_FINAL_BOUNDARY;
                        multipart_ctx->boundary_match_index_ = 0;
                    }
                } else {
                    if (multipart_ctx->is_file_part_) {
                        upload_ctx->upload_buffer_.append(
                            subsequent_boundary.c_str(),
                            multipart_ctx->boundary_match_index_);
                        upload_ctx->upload_buffer_.append(&ch, 1);
                    }
                    multipart_ctx->boundary_match_index_ = 0;
                    state = ACCUMULATE_PART_DATA;
                }
                break;

            case VALIDATE_FINAL_BOUNDARY:
                if (ch == '-') {
                    state = END_MULTIPART;
                } else if (ch == '\r') {
                    state = BOUNDARY_ALMOST_DONE;
                } else {
                    return PARSE_ERROR;
                }
                break;

            case BOUNDARY_ALMOST_DONE:
                if (ch == '\n') {
                    state = READ_PART_HEADERS;
                } else {
                    return PARSE_ERROR;
                }
                break;

            case END_MULTIPART:
                if (ch == '-') {
                    upload_ctx->upload_complete = true;
                    conn->request_data_->body_fully_parsed_ = true;
                    state = FINAL_BOUNDARY_ALMOST_DONE;
                } else {
                    return PARSE_ERROR;
                }
                break;

            case FINAL_BOUNDARY_ALMOST_DONE:
                if (ch == '\r') {
                    buff.consume(1);
                    return PARSE_SUCCESS;
                }
                return PARSE_SUCCESS;

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