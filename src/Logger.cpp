#include "webserv.hpp"

void print_request(const Connection* conn) {
    if (conn == NULL) {
        std::cerr << "Error: connection is NULL" << std::endl;
        return;
    }

    std::cout << "\n==== INCOMING REQUEST (fd: " << conn->client_fd_
              << ") ====\n\n";
    std::cout << "method: " << conn->request_data_->method_ << std::endl;
    std::cout << "uri: " << conn->request_data_->uri_ << std::endl;
    std::cout << "version: " << conn->request_data_->version_ << std::endl;
    std::cout << "headers: " << std::endl;
    for (std::map<std::string, std::string>::const_iterator it =
             conn->request_data_->headers_.begin();
         it != conn->request_data_->headers_.end(); ++it) {
        std::cout << "  " << it->first << ": " << it->second << std::endl;
    }
    std::cout << "body: " << std::endl;
    std::cout.write(conn->request_data_->body_.data(),
                    conn->request_data_->body_.size());
    std::cout << "Parse status: " << conn->parse_status_ << std::endl;
    std::cout << "\n====================================\n" << std::endl;
}

void print_response(const Connection* conn) {
    if (conn == NULL) {
        std::cerr << "Error: connection is NULL" << std::endl;
        return;
    }

    std::cout << "\n==== HTTP RESPONSE ====\n";
    std::cout << "Status: " << conn->response_data_->status_code_ << " "
              << conn->response_data_->status_message_ << std::endl;
    std::cout << "Headers: ";
    for (std::map<std::string, std::string>::const_iterator it =
             conn->response_data_->headers_.begin();
         it != conn->response_data_->headers_.end(); ++it) {
        std::cout << it->first << "=" << it->second << "; ";
    }
    std::cout << std::endl;
    std::cout << "Body size: " << conn->response_data_->body_.size() << " bytes"
              << std::endl;
    std::cout << "=====================\n" << std::endl;
}

int print_buffer(std::vector<char>& buffer) {
    std::cout << "Buffer content: " << std::endl;
    int bytes_written = write(1, buffer.data(), buffer.size());

    if (bytes_written < 0) {
        std::cerr << "Error writing to buffer" << std::endl;
        return -1;
    }

    if (bytes_written == 0) {
        std::cerr << "Buffer is empty" << std::endl;
    }

    return bytes_written;
}
