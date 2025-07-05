#ifndef FILEUPLOADHANDLER_HPP
#define FILEUPLOADHANDLER_HPP

#include "common.hpp"

struct Connection;
struct FileUploadContext;

// TODO: find where we can fit FileDeleteHandler in our WebServer flow
class FileUploadHandler : public AHandler {
   public:
    FileUploadHandler();
    virtual ~FileUploadHandler();

    virtual Result check_permissions(Connection* conn);
    virtual Result setup_handler(Connection* conn);
    virtual Result handle_file_upload_write(Connection* conn);
    virtual void cleanup_handler(Connection* conn);

   private:
    MultipartParser multipart_parser_;

    bool process_trailing_slash_redirect(Connection* conn);
    void send_success_response(Connection* conn);
    bool copy_temp_to_final_file(const std::string& temp_path,
                                 const std::string& final_path);
    std::string get_upload_directory(Connection* conn);
    bool ensure_upload_directory_exists(
        Connection* conn, const std::string& upload_dir);
    bool create_directory_recursive(Connection* conn,
                                              const std::string& path);
    std::string sanitize_filename(const std::string& filename);

    // Prevent copying
    FileUploadHandler(const FileUploadHandler&);
    FileUploadHandler& operator=(const FileUploadHandler&);
};

struct FileUploadContext {
    int file_fd_;
    std::string temp_path_;
    std::string filename_;
    Buffer upload_buffer_;
    bool upload_complete;

    MultipartContext multipart_context_;

    FileUploadContext() : file_fd_(-1), upload_complete(false) {}
};

#endif  // FILEUPLOADHANDLER_HPP