#ifndef FILEUPLOADHANDLER_HPP
#define FILEUPLOADHANDLER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
struct ServerConfig;
class ResponseWriter;

// Handles file upload requests
class FileUploadHandler : public IHandler {
public:
    // Constructor takes dependencies
    FileUploadHandler(const ServerConfig& config, ResponseWriter& writer);
    virtual ~FileUploadHandler();

    // Implementation of handle method for file uploads
    // - Validates request method (must be POST)
    // - Checks Content-Type (must be multipart/form-data)
    // - Parses multipart form data to extract files
    // - Saves files to appropriate directory
    // - Creates response (success or error)
    virtual void handle(Connection* conn);

private:
    const ServerConfig& config_;
    ResponseWriter& response_writer_;
    
    // Helper methods
    bool parseMultipartFormData(Connection* conn, const std::string& boundary);
    bool saveUploadedFile(const std::string& filename, const std::vector<char>& data, 
                          const std::string& upload_dir);
    std::string extractBoundary(const std::string& content_type);
    std::string sanitizeFilename(const std::string& filename);


    // Prevent copying
    FileUploadHandler(const FileUploadHandler&);
    FileUploadHandler& operator=(const FileUploadHandler&);
};

#endif // UPLOADHANDLER_HPP