#ifndef BUFFER_HPP
#define BUFFER_HPP

#include "webserv.hpp"

class Buffer {
   public:
    // Allocate the buffer with a specific size, e.g., 4096
    Buffer(size_t size = DEFAULT_CHUNK_SIZE)
        : buffer_(size), pos_(0), last_(0) {}

    // How much space is left for writing new data
    size_t writable_space() const { return buffer_.size() - last_; }

    // A pointer to the start of the writable area
    char* write_ptr() { return &buffer_[last_]; }

    // A pointer to the start of the readable data
    const char* read_ptr() const { return &buffer_[pos_]; }

    // How many bytes are available to be parsed
    size_t readable_bytes() const { return last_ - pos_; }

    // How many bytes have been processed so far
    size_t processed_bytes() const { return pos_; }

    // Call this after you've read `bytes_read` from the socket
    void has_written(size_t bytes_written) { last_ += bytes_written; }

    // Call this after you've parsed `bytes_parsed`
    void has_read(size_t bytes_parsed) { pos_ += bytes_parsed; }

    // Get the current size of the buffer
    size_t size() const { return buffer_.size(); }

    // Check if the buffer is empty
    bool empty() const { return pos_ == last_; }

    // Resets the buffer for keep-alive by just moving the pointers
    void reset() {
        pos_ = 0;
        last_ = 0;
    }

   private:
    std::vector<char> buffer_;
    size_t pos_;  // Current position in the buffer for reading
    size_t
        last_;  // Last position in the buffer for writing (end of valid data)
};

#endif  // BUFFER_HPP