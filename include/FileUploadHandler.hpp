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

    virtual HttpStatus check_permissions(Connection* conn);
    virtual HttpStatus setup_handler(Connection* conn);
    virtual HttpStatus handle_event(Connection* conn);
    virtual void cleanup_handler(Connection* conn);
    virtual bool is_asynchronous() const { return true; }

   private:
    // Request validation and processing
    // bool validate_request(Connection* conn, std::string& boundary);
    HttpStatus process_trailing_slash_redirect(Connection* conn);

    // Response generation
    void send_success_response(Connection* conn);

    // File saving operations
    bool copy_temp_to_final_file(const std::string& temp_path,
                                 const std::string& final_path);
    // bool save_uploaded_file(Connection* conn, const std::string& filename,
    //                         const std::vector<char>& data);
    // bool write_file_to_disk(Connection* conn, const std::string& file_path,
    //                         const std::vector<char>& data);

    // Directory operations
    std::string get_upload_directory(Connection* conn);
    HttpStatus ensure_upload_directory_exists(
        Connection* conn, const std::string& upload_dir);
    HttpStatus create_directory_recursive(Connection* conn,
                                              const std::string& path);

    // Utility methods
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
    MultipartParser parser_;

    FileUploadContext() : file_fd_(-1), upload_complete(false) {}
};

#endif  // FILEUPLOADHANDLER_HPP