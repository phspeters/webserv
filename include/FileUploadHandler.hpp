#ifndef FILEUPLOADHANDLER_HPP
#define FILEUPLOADHANDLER_HPP

#include "common.hpp"

struct Connection;
struct FileUploadContext;

class FileUploadHandler : public AHandler {
   public:
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
    bool copy_temp_to_final_file(const std::string& temp_path,
                                 const std::string& final_path);
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
    int file_fd_;
    Buffer upload_buffer_;
    std::string temp_path_;
    bool upload_complete;
    std::string filename_;

    FileUploadContext() : file_fd_(-1), upload_complete(false) {}
};

#endif  // FILEUPLOADHANDLER_HPP