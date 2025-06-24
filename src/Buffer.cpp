#include "webserv.hpp"

Buffer::Buffer(size_t size = DEFAULT_CHUNK_SIZE)
    : buffer_(size), pos_(0), last_(0) {}

ssize_t Buffer::read_from(int fd) {
    if (writable_space() == 0) {
        compact();
    }
    if (writable_space() == 0) {
        // Buffer is genuinely full of unread data, cannot produce.
        return 0;
    }
    ssize_t bytes_read = read(fd, write_ptr(), writable_space());
    if (bytes_read > 0) {
        has_written(bytes_read);
    }
    return bytes_read;
}

ssize_t Buffer::write_to(int fd) {
    if (empty()) {
        return 0;  // Nothing to consume.
    }
    ssize_t bytes_sent = write(fd, data(), readable_bytes());
    if (bytes_sent > 0) {
        consume(bytes_sent);
    }
    return bytes_sent;
}

ssize_t Buffer::send_to(int fd) {
    if (empty()) {
        return 0;  // Nothing to consume.
    }
    ssize_t bytes_sent = send(fd, data(), readable_bytes(), MSG_NOSIGNAL);
    if (bytes_sent > 0) {
        consume(bytes_sent);
    }
    return bytes_sent;
}

void Buffer::reset() {
    pos_ = 0;
    last_ = 0;
}

void Buffer::prepare_for_next_request() {
    if (readable_bytes() > 0) {
        compact();  // Call the private implementation detail
    } else {
        reset();
    }
}

char Buffer::peek() const {
    if (empty()) {
        log(LOG_WARNING, "Buffer is empty, nothing to peek.");
        return '\0';  // No data to peek.
    }
    return buffer_[pos_];  // Return the first byte of readable data.
}

void Buffer::compact() {
    if (pos_ == 0) {
        return;
    }  // Nothing to compact.
    size_t len = readable_bytes();
    if (len > 0) {
        std::memmove(&buffer_[0], &buffer_[pos_], len);
    }
    pos_ = 0;
    last_ = len;
}
