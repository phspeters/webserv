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
    // Error handling methods
    void handle_upload_error(Connection* conn, codes::UploadError error);

    // Helper methods
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
    bool save_uploaded_file(Connection *conn, const std::string& filename,
                            const std::vector<char>& data, codes::UploadError& error);
    std::string extract_boundary(const std::string& content_type);
    std::string sanitize_filename(const std::string& filename);
    std::string get_upload_directory(Connection* conn);
    bool validate_upload_size(size_t size, size_t max_body_size);
    bool ensure_upload_directory_exists(const std::string& upload_dir,
                                        codes::UploadError& error);
    bool create_directory_recursive(const std::string& path,
                                    codes::UploadError& error);
    bool validate_upload_environment(const std::string& upload_dir,
                                     size_t file_size,
                                     codes::UploadError& error);
    bool check_disk_space(const std::string& upload_dir,
                          size_t file_size,
                          codes::UploadError& error);
    bool write_file_to_disk(const std::string& file_path,
                            const std::vector<char>& data,
                            codes::UploadError& error);

    // Prevent copying
    FileUploadHandler(const FileUploadHandler&);
    FileUploadHandler& operator=(const FileUploadHandler&);
};

#endif  // FILEUPLOADHANDLER_HPP