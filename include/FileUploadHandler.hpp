#ifndef FILEUPLOADHANDLER_HPP
#define FILEUPLOADHANDLER_HPP

#include "webserv.hpp"

// Forward declarations
struct Connection;
struct FileUploadContext;

// Handles file upload requests
class FileUploadHandler : public AHandler {
   public:
    // Constructor takes dependencies
    FileUploadHandler();
    virtual ~FileUploadHandler();

    virtual void check_permissions(Connection* conn);
    virtual void setup_handler(Connection* conn);
    virtual void handle_event(Connection* conn);
    virtual void cleanup_handler(Connection* conn);

   private:
    // Request validation and processing
    bool validate_request(Connection* conn, std::string& boundary);
    bool process_trailing_slash_redirect(Connection* conn);

    // Response generation
    void send_success_response(Connection* conn);

    // File saving operations
    bool save_uploaded_file(Connection* conn, const std::string& filename,
                            const std::vector<char>& data);
    bool write_file_to_disk(Connection* conn, const std::string& file_path,
                            const std::vector<char>& data);

    // Directory operations
    std::string get_upload_directory(Connection* conn);
    bool ensure_upload_directory_exists(Connection* conn,
                                        const std::string& upload_dir);
    bool create_directory_recursive(Connection* conn, const std::string& path);

    // Utility methods
    std::string sanitize_filename(const std::string& filename);

    // Prevent copying
    FileUploadHandler(const FileUploadHandler&);
    FileUploadHandler& operator=(const FileUploadHandler&);
};

struct FileUploadContext {
    // State for file upload handling
    int file_fd_;  // File descriptor for the opened uploaded file (-1 if none)
    Buffer upload_buffer_;  // Buffer for reading file data
    std::string temp_path_; // Caminho do arquivo temporário
    bool upload_complete;   // Indica se o upload terminou
    std::string filename_;  // Nome do arquivo extraído do multipart

    FileUploadContext()
        : file_fd_(-1), upload_complete(false) {
    }  // Initialize file_fd_ to -1 indicating no file opened
};

#endif  // FILEUPLOADHANDLER_HPP