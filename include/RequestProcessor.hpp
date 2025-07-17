#ifndef REQUESTPROCESSOR_HPP
#define REQUESTPROCESSOR_HPP

#include "common.hpp"

class RequestProcessor {
   public:
    RequestProcessor() {};
    ~RequestProcessor() {};

    void match_host_header(Connection* conn,
                           const std::map<int, std::vector<VirtualServer*> >&
                               listener_to_virtual_servers_);
    const Location* match_location(const VirtualServer* virtual_server,
                                   const std::string& path) const;
    bool handle_redirects(Connection* conn);
    bool process_location_redirect(Connection* conn);
    bool process_directory_redirect(Connection* conn,
                                    std::string& request_path);
    ParseStatus validate_version(Connection* conn);
    ParseStatus validate_method_location_access(Connection* conn);
    ParseStatus validate_body_handling(Connection* conn);

   private:
    // Prevent copying
    RequestProcessor(const RequestProcessor&);
    RequestProcessor& operator=(const RequestProcessor&);
};

#endif  // REQUESTPROCESSOR_HPP