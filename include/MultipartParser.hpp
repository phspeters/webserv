#ifndef MULTIPART_PARSER_HPP
#define MULTIPART_PARSER_HPP

#include "common.hpp"

struct MultipartContext;

class MultipartParser {
   public:
    MultipartParser();
    ~MultipartParser();

    ParseStatus parse_multipart(Connection* conn);
    static std::string extract_boundary(const std::string& content_type);

   private:
    // Prevent copying to avoid accidental slicing or ownership issues.
    MultipartParser(const MultipartParser&);
    MultipartParser& operator=(const MultipartParser&);
};

struct MultipartContext {
    MultipartContext()
        : state_(SEARCH_INITIAL_BOUNDARY),
          is_file_part_(false),
          boundary_match_index_(0) {}

    MultipartState state_;
    std::string boundary_;
    std::string part_headers_;
    bool is_file_part_;
    size_t boundary_match_index_;

    void reset() {
        state_ = SEARCH_INITIAL_BOUNDARY;
        boundary_.clear();
        part_headers_.clear();
        is_file_part_ = false;
        boundary_match_index_ = 0;
    }
};

#endif  // MULTIPART_PARSER_HPP
