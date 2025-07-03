#include "common.hpp"

MultipartParser::MultipartParser() {}

MultipartParser::~MultipartParser() {}

ParseStatus MultipartParser::parse(Connection* conn) {
    log(LOG_DEBUG, "Parsing multipart body for connection: %i",
        conn->client_fd_);

    if (!conn->file_upload_context_) {
        return PARSE_ERROR;
    }

    Buffer& buff = conn->read_buffer_;
    FileUploadContext* upload_ctx = conn->file_upload_context_;
    std::string boundary = conn->parser_context_.multipart_boundary_;
    if (boundary.empty()) {
        log(LOG_ERROR, "No multipart boundary set for connection: %i",
            conn->client_fd_);
        return PARSE_ERROR;
    }
    std::string full_boundary = "--" + boundary;
    std::string end_boundary = full_boundary + "--";

    MultipartState& state = reinterpret_cast<MultipartState&>(
        conn->parser_context_.granular_parser_state_);

    while (buff.readable_bytes() > 0) {
        const char* data = buff.data();
        size_t len = buff.readable_bytes();
        std::string chunk(data, len);
        size_t pos = 0;

        switch (state) {
            case SEARCH_BOUNDARY: {
                size_t bpos = chunk.find(full_boundary);
                if (bpos == std::string::npos) {
                    // Boundary not found, consume all and wait for more
                    // data
                    buff.consume(len);
                    return PARSE_INCOMPLETE;
                }
                pos = bpos + full_boundary.length();
                // There may be CRLF after the boundary
                if (pos + 1 < len && chunk.substr(pos, 2) == "\r\n") pos += 2;
                buff.consume(pos);
                state = READ_HEADERS;
                upload_ctx->part_headers_.clear();
                upload_ctx->is_file_part_ = false;
                if (buff.readable_bytes() == 0) return PARSE_INCOMPLETE;
                continue;
            }
            case READ_HEADERS: {
                // Search for end of headers (\r\n\r\n)
                std::string chunk_headers(buff.data(), buff.readable_bytes());
                size_t hpos = chunk_headers.find("\r\n\r\n");
                if (hpos == std::string::npos) {
                    // Headers incomplete, wait for more data
                    return PARSE_INCOMPLETE;
                }
                upload_ctx->part_headers_ = chunk_headers.substr(0, hpos);
                // Check if it's a file part and extract filename
                size_t fnpos = upload_ctx->part_headers_.find("filename=");
                if (fnpos != std::string::npos) {
                    upload_ctx->is_file_part_ = true;
                    // Extract filename from headers
                    size_t start =
                        upload_ctx->part_headers_.find('"', fnpos);
                    size_t end = std::string::npos;
                    if (start != std::string::npos) {
                        end = upload_ctx->part_headers_.find('"', start + 1);
                        if (end != std::string::npos) {
                            upload_ctx->filename_ =
                                upload_ctx->part_headers_.substr(
                                    start + 1, end - start - 1);
                        }
                    }
                } else {
                    upload_ctx->is_file_part_ = false;
                }
                buff.consume(hpos + 4);
                state = READ_FILE_DATA;
                if (buff.readable_bytes() == 0) return PARSE_INCOMPLETE;
                continue;
            }
            case READ_FILE_DATA: {
                // Search for next boundary
                std::string chunk_data(buff.data(), buff.readable_bytes());
                size_t bpos = chunk_data.find(full_boundary);
                if (bpos == std::string::npos) {
                    // Boundary not found, if it's a file part, write
                    // everything
                    if (upload_ctx->is_file_part_) {
                        upload_ctx->upload_buffer_.append(
                            buff.data(), buff.readable_bytes());
                    }
                    buff.consume(buff.readable_bytes());
                    return PARSE_INCOMPLETE;
                }
                // Found boundary, write until it
                if (upload_ctx->is_file_part_ &&
                    bpos > 2) {  // Remove CRLF before the boundary
                    upload_ctx->upload_buffer_.append(buff.data(), bpos - 2);
                }
                buff.consume(bpos);
                // If it's the end boundary, upload complete
                if (chunk_data.substr(bpos, end_boundary.length()) ==
                    end_boundary) {
                    upload_ctx->upload_complete = true;
                    buff.consume(end_boundary.length());
                    state = END_MULTIPART;
                    return PARSE_SUCCESS;
                }
                state = SEARCH_BOUNDARY;
                continue;
            }
            case END_MULTIPART:
                conn->request_data_->body_fully_parsed_ = true;
                return PARSE_SUCCESS;
        }
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