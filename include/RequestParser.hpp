#ifndef REQUESTPARSER_HPP
#define REQUESTPARSER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
struct HttpRequest;
struct VirtualServer;

// Parses HTTP requests incrementally from a Connection's read buffer.
class RequestParser {
   public:
    RequestParser();
    ~RequestParser();

    // Reads data from the socket into the connection's read buffer.
    // - Returns true if data was read successfully.
    // - Returns false if the connection is closed or an error occurs.
    // - If false, the connection should be closed.
    // - If true, the read buffer is updated with the new data.
    bool read_from_socket(Connection* conn);

    // Parses data currently in the connection's read buffer.
    // - Populates conn->request_data if successful (PARSE_SUCCESS).
    // - Updates conn->read_buffer (removing parsed data).
    // - Returns the result status.
    codes::ParseStatus parse(Connection* conn);

   private:
    // Request line parsing methods
    codes::ParseStatus parse_request_line(Connection* conn);
    bool split_request_line(HttpRequest* request, std::string& request_line);
    bool split_uri_components(HttpRequest* request);
    std::string decode_uri_path(const std::string& uri);
    std::string decode_uri_query(const std::string& uri);
    inline int hex_to_int(char c);
    codes::ParseStatus validate_request_line(const HttpRequest* request);
    bool validate_method(const std::string& method);
    bool validate_path(const std::string& path);
    bool validate_query_string(const std::string& query_string);
    bool validate_http_version(const std::string& version);

    // Header parsing methods
    codes::ParseStatus parse_headers(Connection* conn);
    codes::ParseStatus process_single_header(const std::string& header_line,
                                             HttpRequest* request);
    codes::ParseStatus validate_headers(Connection* conn);
    codes::ParseStatus determine_request_body_handling(Connection* conn);

    // Body parsing methods
    codes::ParseStatus parse_body(Connection* conn);
    codes::ParseStatus parse_chunked_body(Connection* conn);
    codes::ParseStatus parse_chunk_header(std::vector<char>& buffer,
                                          size_t& out_chunk_size);
    codes::ParseStatus read_chunk_data(std::vector<char>& buffer,
                                       HttpRequest* request,
                                       size_t& chunk_remaining_bytes,
                                       size_t client_max_body_size);
    codes::ParseStatus process_chunk_terminator(std::vector<char>& buffer);
    codes::ParseStatus finish_chunked_parsing(std::vector<char>& buffer);

    // Prevent copying
    RequestParser(const RequestParser&);
    RequestParser& operator=(const RequestParser&);

};  // class RequestParser

#endif  // REQUESTPARSER_HPP
