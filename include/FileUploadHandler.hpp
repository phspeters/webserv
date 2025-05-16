#ifndef FILEUPLOADHANDLER_HPP
#define FILEUPLOADHANDLER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
struct ServerConfig;

// Handles file upload requests
class FileUploadHandler : public AHandler {
   public:
    // Constructor takes dependencies
    FileUploadHandler(const ServerConfig& config);
    virtual ~FileUploadHandler();

    virtual void handle(Connection* conn);

   private:
    const ServerConfig& config_;

    // Helper methods
    bool parseMultipartFormData(Connection* conn, const std::string& boundary);
    bool saveUploadedFile(const std::string& filename,
                          const std::vector<char>& data);
    std::string extractBoundary(const std::string& content_type);
    std::string sanitizeFilename(const std::string& filename);

    // Prevent copying
    FileUploadHandler(const FileUploadHandler&);
    FileUploadHandler& operator=(const FileUploadHandler&);
};

#endif // FILEUPLOADHANDLER_HPP