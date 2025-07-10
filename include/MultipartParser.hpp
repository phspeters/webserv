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
    void commit_part_data(MultipartContext* multipart_ctx,
                          FileUploadContext* upload_ctx);
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
    size_t data_length_;  // Length of current part data
    const char* data_start_;  // Points to start of current part data
    const char* data_end_;
    bool is_file_part_;

    MultipartContext()
        : state_(FIND_INITIAL_BOUNDARY),
        boundary_match_index_(0),
        data_length_(0),
        data_start_(NULL),
        data_end_(NULL),
        is_file_part_(false) {}

    void reset() {
        state_ = FIND_INITIAL_BOUNDARY;
        boundary_.clear();
        part_headers_.clear();
        boundary_match_index_ = 0;
        data_length_ = 0;
        data_start_ = NULL;
        data_end_ = NULL;
        is_file_part_ = false;
    }
};

#endif  // MULTIPART_PARSER_HPP
