#ifndef MULTIPART_PARSER_HPP
#define MULTIPART_PARSER_HPP

#include "common.hpp"

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

#endif // MULTIPART_PARSER_HPP
