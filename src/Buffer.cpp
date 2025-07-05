#include "common.hpp"

Buffer::Buffer(size_t size) : buffer_(size), pos_(0), last_(0) {}

ssize_t Buffer::read_from(int fd) {
    if (writable_space() == 0) {
        compact();
    }
    if (writable_space() == 0) {
        // Buffer is genuinely full of unread data, cannot read more.
        return BUFFER_FULL;
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

size_t Buffer::unload_to(Buffer& dest, size_t max_bytes) {
    size_t available_space = dest.writable_space();

    size_t bytes_to_move = std::min(readable_bytes(), available_space);
    bytes_to_move = std::min(bytes_to_move, max_bytes);

    if (bytes_to_move == 0) {
        return 0;
    }

    dest.append(data(), bytes_to_move);

    consume(bytes_to_move);

    return bytes_to_move;
}

size_t Buffer::append(const char* data, size_t size) {
    if (size == 0) {
        return 0;
    }

    if (writable_space() == 0) {
        compact();
    }
    if (writable_space() == 0) {
        return BUFFER_FULL;
    }

    size_t bytes_to_append = std::min(size, writable_space());
    std::memcpy(write_ptr(), data, bytes_to_append);
    has_written(bytes_to_append);

    return bytes_to_append;
}

void Buffer::reset() {
    pos_ = 0;
    last_ = 0;
}

void Buffer::prepare_for_next_request() {
    if (readable_bytes() > 0) {
        compact();
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
    }

    size_t len = readable_bytes();
    if (len > 0) {
        std::memmove(&buffer_[0], &buffer_[pos_], len);
    }
    pos_ = 0;
    last_ = len;
}
