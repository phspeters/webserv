#ifndef FILEUPLOADHANDLER_HPP
#define FILEUPLOADHANDLER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;

// Handles file upload requests
class FileUploadHandler : public AHandler {
   public:
    // Upload-specific error types
    enum UploadError {
        UPLOAD_SUCCESS,
        UPLOAD_BAD_REQUEST,        // General 400 errors
        UPLOAD_UNSUPPORTED_MEDIA,  // 415 errors
        UPLOAD_PAYLOAD_TOO_LARGE,  // 413 errors
        UPLOAD_SERVER_ERROR        // 500 errors
    };

    // Constructor takes dependencies
    FileUploadHandler();
    virtual ~FileUploadHandler();

    virtual void handle(Connection* conn);

   private:
    // Error handling methods
    void handle_upload_error(Connection* conn, UploadError error);

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
    bool save_uploaded_file(HttpRequest* req, const std::string& filename,
                            const std::vector<char>& data, UploadError& error);
    std::string extract_boundary(const std::string& content_type);
    std::string sanitize_filename(const std::string& filename);
    std::string get_upload_directory(HttpRequest* req);
    bool validate_upload_size(size_t size, size_t max_body_size);

    // Prevent copying
    FileUploadHandler(const FileUploadHandler&);
    FileUploadHandler& operator=(const FileUploadHandler&);
};

#endif  // FILEUPLOADHANDLER_HPP