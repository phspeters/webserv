#ifndef FILEUPLOADHANDLER_HPP
#define FILEUPLOADHANDLER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
struct VirtualServer;

// Handles file upload requests
class FileUploadHandler : public AHandler {
   public:
    // Constructor takes dependencies
    FileUploadHandler();
    virtual ~FileUploadHandler();

    virtual void handle(Connection* conn);

   private:
    // Helper methods
    bool parse_multipart_form_data(Connection* conn, const std::string& boundary);
    bool save_uploaded_file(HttpRequest* req, const std::string& filename,
                          const std::vector<char>& data);
    std::string extract_boundary(const std::string& content_type);
    std::string sanitize_filename(const std::string& filename);
    std::string get_upload_directory(HttpRequest* req);

    // Prevent copying
    FileUploadHandler(const FileUploadHandler&);
    FileUploadHandler& operator=(const FileUploadHandler&);
};

#endif // FILEUPLOADHANDLER_HPP