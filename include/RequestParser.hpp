#ifndef REQUESTPARSER_HPP
#define REQUESTPARSER_HPP

#include "common.hpp"

struct Connection;
struct HttpRequest;
struct ParserContext;

class RequestParser {
   public:
    RequestParser() {}
    ~RequestParser() {}

    ParseStatus parse_request_line(Connection* conn);
    ParseStatus parse_headers(Connection* conn);
    ParseStatus parse_content_body(Connection* conn);
    ParseStatus parse_chunked_body(Connection* conn);

   private:
    ParseStatus commit_request_line(HttpRequest* request,
                                    const ParserContext& context);
    std::string decode_uri_path(const std::string& uri);
    std::string normalize_path(const std::string& decoded_path);
    std::string decode_uri_query(const std::string& uri);
    inline int hex_to_int(char c);
    void commit_header(HttpRequest* request, const ParserContext& context);
    ConnectionState determine_body_handling_state(Connection* conn);

    // Prevent copying
    RequestParser(const RequestParser&);
    RequestParser& operator=(const RequestParser&);

};  // class RequestParser

struct ParserContext {
    ParserContext() { reset(); }

    ParserState parser_state_;  // Main parser state

    unsigned int
        granular_parser_state_;  // Current state inside parser functions
    unsigned int return_state_;  // State to return to after parsing

    size_t total_bytes_processed_;
    size_t body_remaining_bytes_;
    size_t chunk_remaining_bytes_;

    const char* method_start_;
    const char* method_end_;
    const char* uri_start_;
    const char* uri_end_;
    const char* path_start_;
    const char* path_end_;
    const char* query_start_;
    const char* query_end_;
    const char* key_start_;
    const char* key_end_;
    const char* value_start_;
    const char* value_end_;
    int version_major_;
    int version_minor_;
    bool sent_100_continue_;

    void reset() {
        parser_state_ = PARSER_READING_REQUEST_LINE;
        clear_for_next_state();
    }

    void clear_for_next_state() {
        granular_parser_state_ = 0;
        return_state_ = 0;
        total_bytes_processed_ = 0;
        body_remaining_bytes_ = 0;
        chunk_remaining_bytes_ = 0;
        method_start_ = NULL;
        method_end_ = NULL;
        uri_start_ = NULL;
        uri_end_ = NULL;
        path_start_ = NULL;
        path_end_ = NULL;
        query_start_ = NULL;
        query_end_ = NULL;
        key_start_ = NULL;
        key_end_ = NULL;
        value_start_ = NULL;
        value_end_ = NULL;
        version_major_ = 0;
        version_minor_ = 0;
        sent_100_continue_ = false;
    }

};  // struct ParserContext

#endif  // REQUESTPARSER_HPP