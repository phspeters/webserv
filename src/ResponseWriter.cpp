#include "common.hpp"

Result ResponseWriter::write_response_to_buffer(Connection* conn) {
    if (!conn || !conn->response_data_) {
        return ERROR;
    }

    log(LOG_TRACE,
        "ResponseWriter::write_response_to_buffer called for client %d",
        conn->client_fd_);

    HttpResponse* resp = conn->response_data_;
    WriterContext& context = conn->writer_context_;
    Result result = COMPLETE;

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
                result = write_response_head(conn);
                if (result == COMPLETE) {
                    context.response_writer_state_ = WRITER_DECIDE_BODY_SOURCE;
                } else {
                    return result;
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
                result = write_response_body_from_buffer(conn);
                if (result == COMPLETE) {
                    if (resp->body_fd_ != -1) {
                        context.response_writer_state_ =
                            WRITER_WRITING_BODY_FROM_FD;
                    } else {
                        context.response_writer_state_ = WRITER_DONE;
                    }
                } else {
                    return result;
                }
                continue;
            }

            case WRITER_WRITING_BODY_FROM_FD: {
                result = write_response_body_from_fd(conn);
                if (result == COMPLETE) {
                    context.response_writer_state_ = WRITER_DONE;
                } else {
                    return result;
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
    log(LOG_TRACE,
        "ResponseWriter::get_response_head_string called for response %p",
        resp);

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
    } else if (resp->content_length_ == 0) {
        // If content length is zero, ensure it's set correctly
        resp->content_length_ =
            std::strtoul(resp->get_header("Content-Length").c_str(), NULL, 10);
    }

    headers << "\r\n";  // End of headers

    return headers.str();
}

Result ResponseWriter::write_response_head(Connection* conn) {
    log(LOG_TRACE, "ResponseWriter::write_response_head called for client %d",
        conn->client_fd_);

    WriterContext& context = conn->writer_context_;

    size_t bytes_to_write = context.formatted_headers_.size();
    size_t bytes_written = conn->write_buffer_.append(
        context.formatted_headers_.c_str(), bytes_to_write);

    log(LOG_DEBUG,
        "ResponseWriter::write_response_head wrote %zu bytes to buffer for "
        "client %d",
        bytes_written, conn->client_fd_);

    if (bytes_written < bytes_to_write) {
        // Buffer is full, store remaining and return
        context.formatted_headers_.erase(0, bytes_written);
        return AGAIN;
    }

    return COMPLETE;
}

Result ResponseWriter::write_response_body_from_buffer(Connection* conn) {
    log(LOG_TRACE,
        "ResponseWriter::write_response_body_from_buffer for client %d",
        conn->client_fd_);

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

    log(LOG_DEBUG,
        "ResponseWriter::write_response_body_from_buffer wrote %zu bytes to "
        "buffer for client %d",
        bytes_written, conn->client_fd_);

    conn->writer_context_.body_bytes_written_ += bytes_written;

    if (bytes_written < bytes_to_write) {
        // Buffer is full, store remaining and return
        return AGAIN;
    }

    // All body data written
    return COMPLETE;
}

Result ResponseWriter::write_response_body_from_fd(Connection* conn) {
    log(LOG_TRACE,
        "ResponseWriter::write_response_body_from_fd called for connection %d",
        conn->client_fd_);

    HttpResponse* resp = conn->response_data_;
    Buffer& buffer = conn->write_buffer_;
    ssize_t bytes_written = buffer.read_from(resp->body_fd_);

    log(LOG_DEBUG,
        "ResponseWriter::write_response_body_from_fd wrote %zd bytes to buffer "
        "for client %d",
        bytes_written, conn->client_fd_);

    conn->writer_context_.body_bytes_written_ += bytes_written;

    if ((resp->content_length_ == conn->writer_context_.body_bytes_written_) ||
        bytes_written < 0) {
        return COMPLETE;
    }

    return AGAIN;
}
