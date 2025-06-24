#ifndef BUFFER_HPP
#define BUFFER_HPP

#include "webserv.hpp"

// TODO: Find way to tell apart read/write operations 0 bytes when full buffer
// from empty fd

// A fixed-size buffer designed for non-blocking I/O.
// It encapsulates the read/write logic to provide a high-level interface
class Buffer {
   public:
    // Allocate the buffer with a specific size, e.g., 4096
    Buffer(size_t size = DEFAULT_CHUNK_SIZE);

    //------------------------------------------------------------------
    // High-Level I/O API (The only way to move data in/out of the buffer)
    //------------------------------------------------------------------

    // Reads from a file descriptor directly into the buffer's writable space.
    // Automatically handles compaction to maximize space.
    // Returns the result of the underlying read() call.
    ssize_t read_from(int fd);

    // Writes readable data from the buffer directly to a file descriptor.
    // Returns the result of the underlying write() call.
    ssize_t write_to(int fd);

    // Send readable data from the buffer directly to a socket.
    // Returns the result of the underlying send() call.
    ssize_t send_to(int fd);

    //------------------------------------------------------------------
    // Public State & Management
    //------------------------------------------------------------------

    // How many bytes are available to be parsed/consumed.
    size_t readable_bytes() const { return last_ - pos_; }

    // Check if there is no data to be read.
    bool empty() const { return pos_ == last_; }

    // Resets the buffer for a new operation (e.g., keep-alive).
    void reset();

    // Prepares the buffer for a subsequent request in a keep-alive connection.
    // If there is pipelined data, it is compacted. Otherwise, the buffer is
    // reset.
    void prepare_for_next_request();

    char peek() const;

    // Returns a pointer to the start of the readable data.
    const char* data() const { return &buffer_[pos_]; }

    // Informs the buffer that `bytes_parsed` have been processed by a parser.
    void consume(size_t bytes_parsed) { pos_ += bytes_parsed; }

   private:
    //------------------------------------------------------------------
    // Internal Implementation Details
    //------------------------------------------------------------------

    // Slides unread data to the beginning to maximize writable space.
    void compact();

    // How much space is left for writing new data.
    size_t writable_space() const { return buffer_.size() - last_; }

    // A pointer to the start of the writable area.
    char* write_ptr() { return &buffer_[last_]; }

    // Informs the buffer that `bytes_written` have been produced.
    void has_written(size_t bytes_written) { last_ += bytes_written; }

    std::vector<char> buffer_;
    size_t pos_;   // Start of readable data.
    size_t last_;  // End of readable data (start of writable space).
};

#endif  // BUFFER_HPP