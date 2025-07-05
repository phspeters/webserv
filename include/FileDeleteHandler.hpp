#ifndef FILEDELETEHANDLER_HPP
#define FILEDELETEHANDLER_HPP

#include "common.hpp"

struct Connection;

class FileDeleteHandler : public AHandler {
   public:
    FileDeleteHandler();
    virtual ~FileDeleteHandler();

    virtual Result check_permissions(Connection* conn);
    virtual Result setup_handler(Connection* conn);
    virtual void cleanup_handler(Connection* conn);

   private:
    // File operations
    bool delete_file(Connection* conn, const std::string& file_path);

    // Prevent copying
    FileDeleteHandler(const FileDeleteHandler&);
    FileDeleteHandler& operator=(const FileDeleteHandler&);
};

#endif  // FILEDELETEHANDLER_HPP