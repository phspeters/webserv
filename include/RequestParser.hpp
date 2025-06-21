#ifndef REQUESTPARSER_HPP
#define REQUESTPARSER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
struct HttpRequest;
struct VirtualServer;

struct ParserContext {
    ParserContext() { reset(); }

    unsigned int parser_state_;  // Current state of the request parser
    unsigned int return_state_;  // State to return to after parsing

    size_t chunk_remaining_bytes_;  // Remaining bytes in the current chunk

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

    void reset() {
        parser_state_ = 0;
        return_state_ = 0;
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
    }

};  // class ParserContext

// Parses HTTP requests incrementally from a Connection's read buffer.
class RequestParser {
   public:
    RequestParser();
    ~RequestParser();

    bool read_from_socket(Connection* conn);
    codes::ParseStatus parse_request_line(Connection* conn);
    codes::ParseStatus parse_headers(Connection* conn);
    codes::ParseStatus process_request(Connection* conn);
    codes::ConnectionState determine_body_handling_state(Connection* conn);
    codes::ParseStatus parse_body(Connection* conn);
    codes::ParseStatus parse_chunked_body(Connection* conn);

   private:
    // Request line parsing methods
    codes::ParseStatus commit_request_line(HttpRequest* request,
                                           const ParserContext& context);
    std::string decode_uri_path(const std::string& uri);
    std::string normalize_path(const std::string& decoded_path);
    std::string decode_uri_query(const std::string& uri);
    inline int hex_to_int(char c);
    void commit_header(HttpRequest* request, const ParserContext& context);

    // DELETE OR REUSE
    codes::ParseStatus validate_request_line(const HttpRequest* request);
    bool validate_method(const std::string& method);
    bool validate_path(const std::string& path);
    bool validate_query_string(const std::string& query_string);
    bool validate_http_version(const std::string& version);

    // Header parsing methods
    codes::ParseStatus process_single_header(const std::string& header_line,
                                             HttpRequest* request);
    codes::ParseStatus validate_headers(Connection* conn);

    // Body parsing methods
    codes::ParseStatus parse_chunk_header(std::vector<char>& buffer,
                                          size_t& out_chunk_size);
    codes::ParseStatus read_chunk_data(std::vector<char>& buffer,
                                       HttpRequest* request,
                                       size_t& chunk_remaining_bytes,
                                       size_t client_max_body_size);
    codes::ParseStatus process_chunk_terminator(std::vector<char>& buffer);
    codes::ParseStatus finish_chunked_parsing(std::vector<char>& buffer);
    // DELETE OR REUSE

    // Prevent copying
    RequestParser(const RequestParser&);
    RequestParser& operator=(const RequestParser&);

};  // class RequestParser

#endif  // REQUESTPARSER_HPP
