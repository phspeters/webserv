#ifndef MULTIPART_PARSER_HPP
#define MULTIPART_PARSER_HPP

#include "common.hpp"

struct MultipartContext;
struct FileUploadContext;

class MultipartParser {
   public:
    MultipartParser();
    ~MultipartParser();

    ParseStatus parse_multipart(Connection* conn);
    static std::string extract_boundary(const std::string& content_type);

   private:
    void parse_part_headers(const std::string& headers,
                            FileUploadContext* upload_ctx);

    // Prevent copying to avoid accidental slicing or ownership issues.
    MultipartParser(const MultipartParser&);
    MultipartParser& operator=(const MultipartParser&);
};

struct MultipartContext {
    MultipartState state_;
    std::string boundary_;
    std::string part_headers_;
    size_t boundary_match_index_;
    bool is_file_part_;

    MultipartContext()
        : state_(FIND_INITIAL_BOUNDARY),
          boundary_match_index_(0),
          is_file_part_(false) {}

    void reset() {
        state_ = FIND_INITIAL_BOUNDARY;
        boundary_.clear();
        part_headers_.clear();
        boundary_match_index_ = 0;
        is_file_part_ = false;
    }
};

#endif  // MULTIPART_PARSER_HPP
