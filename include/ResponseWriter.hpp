#ifndef RESPONSEWRITER_HPP
#define RESPONSEWRITER_HPP

#include "common.hpp"

struct Connection;
struct HttpResponse;
struct WriterContext;

class ResponseWriter {
   public:
    ResponseWriter() {}
    ~ResponseWriter() {}

    Result write_response_to_buffer(Connection* conn);

   private:
    std::string get_response_head_string(HttpResponse* resp);
    Result write_response_head(Connection* conn);
    Result write_response_body_from_buffer(Connection* conn);
    Result write_response_body_from_fd(Connection* conn);

    // Prevent copying
    ResponseWriter(const ResponseWriter&);
    ResponseWriter& operator=(const ResponseWriter&);

};  // class ResponseWriter

struct WriterContext {
    WriterState response_writer_state_;
    std::string formatted_headers_;
    size_t body_bytes_written_;

    WriterContext()
        : response_writer_state_(WRITER_START), body_bytes_written_(0) {}

    void reset() {
        response_writer_state_ = WRITER_START;
        formatted_headers_.clear();
        body_bytes_written_ = 0;
    }
};

#endif  // RESPONSEWRITER_HPP