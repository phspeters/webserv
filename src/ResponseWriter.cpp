#include "common.hpp"

Result ResponseWriter::write_response_to_buffer(Connection* conn) {
    if (!conn || !conn->response_data_) {
        return ERROR;
    }

    HttpResponse* resp = conn->response_data_;
    WriterContext& context = conn->writer_context_;
    Result status = COMPLETE;

    while (context.response_writer_state_ != WRITER_DONE) {
        switch (context.response_writer_state_) {
            case WRITER_START: {
                context.formatted_headers_ = get_response_head_string(resp);
                context.response_writer_state_ = WRITER_WRITING_HEADERS;
                continue;
            }

            case WRITER_WRITING_HEADERS: {
                if (context.formatted_headers_.empty()) {
                    context.response_writer_state_ = WRITER_DECIDE_BODY_SOURCE;
                }
                status = write_response_head(conn);
                if (status == COMPLETE) {
                    context.response_writer_state_ = WRITER_DECIDE_BODY_SOURCE;
                } else {
                    return status;
                }
                continue;
            }

            case WRITER_DECIDE_BODY_SOURCE: {
                if (!resp->body_data_.empty()) {
                    context.response_writer_state_ =
                        WRITER_WRITING_BODY_FROM_BUFFER;
                } else if (resp->body_fd_ != -1) {
                    context.response_writer_state_ =
                        WRITER_WRITING_BODY_FROM_FD;
                } else {
                    context.response_writer_state_ = WRITER_DONE;
                }
                continue;
            }

            case WRITER_WRITING_BODY_FROM_BUFFER: {
                status = write_response_body_from_buffer(conn);
                if (status == COMPLETE) {
                    if (resp->body_fd_ != -1) {
                        context.response_writer_state_ =
                            WRITER_WRITING_BODY_FROM_FD;
                    } else {
                        context.response_writer_state_ = WRITER_DONE;
                    }
                } else {
                    return status;
                }
                continue;
            }

            case WRITER_WRITING_BODY_FROM_FD: {
                status = write_response_body_from_fd(conn);
                if (status == COMPLETE) {
                    context.response_writer_state_ = WRITER_DONE;
                } else {
                    return status;
                }
                continue;
            }

            case WRITER_DONE:
                break;
        }
    }

    return COMPLETE;
}

std::string ResponseWriter::get_response_head_string(HttpResponse* resp) {
    std::stringstream headers;
    headers << resp->version_ << " " << resp->status_code_ << " "
            << get_status_message(resp->status_code_) << "\r\n";

    for (std::map<std::string, std::string>::const_iterator it =
             resp->headers_.begin();
         it != resp->headers_.end(); ++it) {
        headers << it->first << ": " << it->second << "\r\n";
    }

    // Add default headers if not already set
    if (resp->headers_.find("date") == resp->headers_.end()) {
        headers << "Date: " << get_current_gmt_time() << "\r\n";
    }

    if (resp->headers_.find("server") == resp->headers_.end()) {
        headers << "Server: Webserv/4.2\r\n";
    }

    if (resp->headers_.find("content-type") == resp->headers_.end() &&
        !resp->content_type_.empty()) {
        headers << "Content-Type: " << resp->content_type_ << "\r\n";
    }

    if (resp->headers_.find("content-length") == resp->headers_.end()) {
        headers << "Content-Length: " << resp->content_length_ << "\r\n";
    }

    if (resp->headers_.find("connection") == resp->headers_.end()) {
        headers << "Connection: "
                << (resp->get_header("Connection") == "close" ? "close"
                                                              : "keep-alive")
                << "\r\n";
    }

    headers << "\r\n";  // End of headers

    return headers.str();
}

Result ResponseWriter::write_response_head(Connection* conn) {
    WriterContext& context = conn->writer_context_;

    size_t bytes_to_write = context.formatted_headers_.size();
    size_t bytes_written = conn->write_buffer_.append(
        context.formatted_headers_.c_str(), bytes_to_write);

    if (bytes_written < bytes_to_write) {
        // Buffer is full, store remaining and return
        context.formatted_headers_.erase(0, bytes_written);
        return AGAIN;
    }

    context.formatted_headers_.clear();
    return COMPLETE;
}

Result ResponseWriter::write_response_body_from_buffer(Connection* conn) {
    HttpResponse* resp = conn->response_data_;
    Buffer& buffer = conn->write_buffer_;

    size_t bytes_to_write =
        resp->body_data_.size() - conn->writer_context_.body_bytes_written_;
    if (bytes_to_write == 0) {
        return COMPLETE;
    }

    size_t bytes_written = buffer.append(
        resp->body_data_.data() + conn->writer_context_.body_bytes_written_,
        bytes_to_write);

    if (bytes_written < bytes_to_write) {
        // Buffer is full, store remaining and return
        conn->writer_context_.body_bytes_written_ += bytes_written;
        return AGAIN;
    }

    // All body data written
    conn->writer_context_.body_bytes_written_ += bytes_written;
    return COMPLETE;
}

Result ResponseWriter::write_response_body_from_fd(Connection* conn) {
    HttpResponse* resp = conn->response_data_;
    Buffer& buffer = conn->write_buffer_;

    ssize_t bytes_written = buffer.read_from(resp->body_fd_);
    if (bytes_written < 0) {
        return ERROR;
    } else if (bytes_written == 0) {
        // EOF reached, no more data to write
        return COMPLETE;
    }

    conn->writer_context_.body_bytes_written_ += bytes_written;
    return AGAIN;
}
