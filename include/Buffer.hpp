#ifndef BUFFER_HPP
#define BUFFER_HPP

#include "common.hpp"

static const ssize_t BUFFER_FULL = -2;

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

    // Unloads data from this buffer, appending it to the destination vector,
    // respecting the vector's capacity. Returns the number of bytes actually
    // unloaded.
    size_t unload_to(std::vector<char>& dest,
                     size_t max_bytes = DEFAULT_CHUNK_SIZE);

    // Appends data to the buffer, up to the available writable space.
    size_t append(const char* data, size_t size);

    //------------------------------------------------------------------
    // Public State & Management
    //------------------------------------------------------------------

    char peek() const;

    const char* data() const { return &buffer_[pos_]; }

    void consume(size_t bytes_parsed) { pos_ += bytes_parsed; }

    size_t readable_bytes() const { return last_ - pos_; }

    bool empty() const { return pos_ == last_; }

    void reset();

    // Prepares the buffer for a subsequent request in a keep-alive connection.
    // If there is pipelined data, it is compacted. Otherwise, the buffer is
    // reset.
    void prepare_for_next_request();

   private:

    // Slides unread data to the beginning to maximize writable space.
    void compact();
 
    size_t writable_space() const { return buffer_.size() - last_; }
 
    char* write_ptr() { return &buffer_[last_]; }
 
    void has_written(size_t bytes_written) { last_ += bytes_written; }
    std::vector<char> buffer_;
    size_t pos_;   // Start of readable data.
    size_t last_;  // End of readable data (start of writable space).
};

#endif  // BUFFER_HPP