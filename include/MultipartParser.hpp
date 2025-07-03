#ifndef MULTIPART_PARSER_HPP
#define MULTIPART_PARSER_HPP

#include "common.hpp"

struct MultipartContext;

class MultipartParser {
   public:
    MultipartParser();
    ~MultipartParser();

    ParseStatus parse(Connection* conn);
    static std::string extract_boundary(const std::string& content_type);

   private:
    // Prevent copying to avoid accidental slicing or ownership issues.
    MultipartParser(const MultipartParser&);
    MultipartParser& operator=(const MultipartParser&);
};

struct MultipartContext {
    MultipartContext() : state_(SEARCH_BOUNDARY), boundary_len_(0) {}

    MultipartState state_;
    std::string boundary_;
    size_t boundary_len_;

    void reset() {
        state_ = SEARCH_BOUNDARY;
        boundary_.clear();
        boundary_len_ = 0;
    }
};

#endif  // MULTIPART_PARSER_HPP
