#ifndef FILEUPLOADHANDLER_HPP
#define FILEUPLOADHANDLER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;

// Handles file upload requests
class FileUploadHandler : public AHandler {
   public:
    // Constructor takes dependencies
    FileUploadHandler();
    virtual ~FileUploadHandler();

    virtual void handle(Connection* conn);

   private:
    // Request validation and processing
    bool validate_request(Connection* conn, std::string& boundary);
    bool process_trailing_slash_redirect(Connection* conn);
    
    // Response generation
    void send_success_response(Connection* conn);
    
    // Error handling methods
    void handle_upload_error(Connection* conn, codes::UploadError error);

    // Multipart form data parsing
    bool parse_multipart_form_data(Connection* conn,
                                   const std::string& boundary);
    bool process_part(Connection* conn, const std::string& body,
                      const std::string& full_boundary,
                      const std::string& end_boundary, size_t& pos,
                      bool& file_found);
    bool extract_part_headers(const std::string& body, size_t& pos,
                              size_t& headers_end, std::string& headers);
    bool extract_filename(const std::string& headers, std::string& filename);
    bool extract_file_content(Connection* conn, const std::string& body,
                              size_t pos, size_t& content_end,
                              const std::string& full_boundary,
                              const std::string& end_boundary,
                              const std::string& filename, bool& file_found);
    
    // File saving operations
    bool save_uploaded_file(Connection *conn, const std::string& filename,
                            const std::vector<char>& data, codes::UploadError& error);
    bool write_file_to_disk(const std::string& file_path,
                            const std::vector<char>& data,
                            codes::UploadError& error);
    
    // Directory operations
    std::string get_upload_directory(Connection* conn);
    bool ensure_upload_directory_exists(const std::string& upload_dir,
                                        codes::UploadError& error);
    bool create_directory_recursive(const std::string& path,
                                    codes::UploadError& error);
    
    // Utility methods
    std::string extract_boundary(const std::string& content_type);
    std::string sanitize_filename(const std::string& filename);

    // Prevent copying
    FileUploadHandler(const FileUploadHandler&);
    FileUploadHandler& operator=(const FileUploadHandler&);
};

#endif  // FILEUPLOADHANDLER_HPP