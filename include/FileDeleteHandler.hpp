#ifndef FILEDELETEHANDLER_HPP
#define FILEDELETEHANDLER_HPP

#include "common.hpp"

struct Connection;

class FileDeleteHandler : public AHandler {
   public:
    FileDeleteHandler();
    virtual ~FileDeleteHandler();

    virtual ResponseStatus check_permissions(Connection* conn);
    virtual ResponseStatus setup_handler(Connection* conn);
    virtual ResponseStatus handle_event(Connection* conn);
    virtual void cleanup_handler(Connection* conn);

   private:
    // Request validation and processing
    bool validate_delete_request(Connection* conn);
    bool extract_file_path(Connection* conn, std::string& file_path);

    // File operations
    bool delete_file(Connection* conn, const std::string& file_path);

    // Response generation
    void send_delete_success_response(Connection* conn,
                                      const std::string& filename);

    // Prevent copying
    FileDeleteHandler(const FileDeleteHandler&);
    FileDeleteHandler& operator=(const FileDeleteHandler&);
};

#endif  // FILEDELETEHANDLER_HPP