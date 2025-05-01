#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

#include "webserv.hpp"

class ServerBlock;
class RouteConfig;

struct ServerConfig {
   public:
    // Configuration Members
    unsigned short port_;
    std::string server_name_;
    std::string server_root_;
    std::string default_index_;
    size_t max_request_body_size_;
    int connection_timeout_seconds_;

    // Global Handlers/Types
    std::map<std::string, std::string> mime_types_;
    std::map<std::string, std::string> cgi_executables_;

    // Parsed Server Blocks
    std::vector<ServerBlock> servers_;

    // --- Public Methods ---
    ServerConfig();  // Constructor - sets defaults

    bool load_from_file(const std::string& filename);

    // Find appropriate server/route for a request
    const ServerBlock* find_server_block(
        const std::string& listen_host,         // Host server is listening on
        unsigned short listen_port,             // Port server is listening on
        const std::string& host_header) const;  // Host header from request

    const RouteConfig* find_route(const ServerBlock* server,
                                  const std::string& path) const;

    const RouteConfig* find_route_for_request(const std::string& listen_host,
                                              unsigned short listen_port,
                                              const std::string& host_header,
                                              const std::string& path) const;

    // MIME and CGI lookup
    std::string get_mime_type(const std::string& filename) const;
    bool is_cgi_extension(const std::string& filename) const;
    std::string get_cgi_handler(const std::string& filename) const;

   private:
    // --- Private Helper Methods ---

    // Initialization Helpers
    void set_default_mime_types();
    void set_default_cgi_handlers();
    void set_default_server_block();

    // String/File Helpers 
    std::string get_file_extension(const std::string& filename) const;
    bool skip_empty_and_comment_lines(std::istream& stream, std::string& line);
    std::string trim_string(const std::string& str);

    // Line Parsing Helpers
    void parse_line(const std::string& line, std::map<std::string, std::string>& directives);
    void handle_cgi_ext_directive(std::map<std::string, std::string>& directives, const std::string& line);
    void handle_key_only_directive(std::map<std::string, std::string>& directives, const std::string& trimmed_line, size_t first_char);
    void handle_key_value_directive(std::map<std::string, std::string>& directives, const std::string& trimmed_line, size_t first_char, size_t first_space);

    // --- Directive Parsing ---
    // Server Level
    bool parse_listen_directive(ServerBlock& server, const std::string& value);
    void parse_server_name_directive(ServerBlock& server, const std::string& value);
    bool parse_max_body_size_directive(ServerBlock& server, const std::string& value);
    bool parse_error_page_directive(ServerBlock& server, const std::string& value);
    // Location Level
    bool parse_return_directive(RouteConfig& route, const std::string& value);
    void parse_methods_directive(RouteConfig& route, const std::string& value);
    
    // --- Directive Processors ---
    bool process_server_directive(ServerBlock& server, const std::string& key, const std::string& value);
    bool process_location_directive(RouteConfig& route, const std::string& key, const std::string& value);

    // Block Parsing
    bool parse_server_block(std::istream& config_file);
    bool parse_location_block(std::istream& config_file, RouteConfig& route);

    // Location Block Helpers
    bool validate_location_path(const std::string& line, std::string& path);
    bool find_opening_brace(std::istream& config_file, std::string& current_line, size_t& brace_pos, const std::string& context_label);
    
    // Post-Parsing / Finalization Helpers                                 
    bool handle_empty_server_config(const std::string& filename);
    void update_config_from_first_server();

    // Matching Logic Helpers 
    bool is_route_match(const RouteConfig& route, const std::string& path) const;
};

#endif  // SERVERCONFIG_HPP