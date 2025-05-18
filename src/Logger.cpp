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
    std::cout << "Parse status: " << conn->request_data_->parse_status_
              << std::endl;
    std::cout << "\n====================================\n" << std::endl;
}

void print_response(const Connection* conn) {
    if (conn == NULL) {
        std::cerr << "Error: connection is NULL" << std::endl;
        return;
    }

    std::cout << "\n==== OUTGOING RESPONSE (fd: " << conn->client_fd_
              << ") ====\n\n";
    std::cout << "status_code: " << conn->response_data_->status_code_
              << std::endl;
    std::cout << "status_message: " << conn->response_data_->status_message_
              << std::endl;
    std::cout << "version: " << conn->response_data_->version_ << std::endl;
    std::cout << "headers: " << std::endl;
    for (std::map<std::string, std::string>::const_iterator it =
             conn->response_data_->headers_.begin();
         it != conn->response_data_->headers_.end(); ++it) {
        std::cout << "  " << it->first << ": " << it->second << std::endl;
    }
    std::cout << "body: " << std::endl;
    std::cout.write(conn->response_data_->body_.data(),
                    conn->response_data_->body_.size());
    std::cout << "\n====================================\n" << std::endl;
}

int print_and_erase_buffer(std::vector<char>& buffer) {
    std::cout << "Buffer content: " << std::endl;
    int bytes_written = write(1, buffer.data(), buffer.size());
    buffer.erase(buffer.begin(), buffer.begin() + bytes_written);

    if (bytes_written < 0) {
        std::cerr << "Error writing to buffer" << std::endl;
        return -1;
    }

    if (bytes_written == 0) {
        std::cerr << "Buffer is empty" << std::endl;
    }

    return bytes_written;
}

void print_server_config(const ServerConfig& config) {
    std::cout << "---------- SERVER CONFIG ----------" << std::endl;
    std::cout << "Host: " << config.host_ << std::endl;
    std::cout << "Port: " << config.port_ << std::endl;

    // Print server names
    std::cout << "Server Names: ";
    if (config.server_names_.empty()) {
        std::cout << "(default server)";
    } else {
        for (size_t i = 0; i < config.server_names_.size(); ++i) {
            std::cout << config.server_names_[i];
            if (i < config.server_names_.size() - 1) {
                std::cout << ", ";
            }
        }
    }
    std::cout << std::endl;

    // Print client max body size with unit
    std::cout << "Client Max Body Size: ";
    if (config.client_max_body_size_ >= 1024 * 1024 * 1024) {
        std::cout << (config.client_max_body_size_ / (1024 * 1024 * 1024)) << "G";
    } else if (config.client_max_body_size_ >= 1024 * 1024) {
        std::cout << (config.client_max_body_size_ / (1024 * 1024)) << "M";
    } else if (config.client_max_body_size_ >= 1024) {
        std::cout << (config.client_max_body_size_ / 1024) << "K";
    } else {
        std::cout << config.client_max_body_size_ << " bytes";
    }
    std::cout << std::endl;

    // Print error pages
    std::cout << "Error Pages:" << std::endl;
    if (config.error_pages.empty()) {
        std::cout << "  (none)" << std::endl;
    } else {
        for (std::map<int, std::string>::const_iterator it =
                 config.error_pages.begin();
             it != config.error_pages.end(); ++it) {
            std::cout << "  " << it->first << " -> " << it->second << std::endl;
        }
    }

    // Print location blocks
    std::cout << "Location Blocks (" << config.locations.size() << "):" << std::endl;
    for (size_t i = 0; i < config.locations.size(); ++i) {
        const LocationConfig& loc = config.locations[i];
        std::cout << "  ---------- LOCATION: " << loc.path << " ----------"
                  << std::endl;

        std::cout << "    root: " << loc.root << std::endl;

        std::cout << "    autoindex: " << (loc.autoindex ? "on" : "off")
                  << std::endl;

        std::cout << "    allowed_methods: ";
        for (size_t j = 0; j < loc.allowed_methods.size(); ++j) {
            std::cout << loc.allowed_methods[j];
            if (j < loc.allowed_methods.size() - 1) {
                std::cout << ", ";
            }
        }
        std::cout << std::endl;

        std::cout << "    cgi: " << (loc.cgi_enabled ? "on" : "off")
                  << std::endl;

        std::cout << "    index: " << loc.index << std::endl;

        if (!loc.redirect.empty()) {
            std::cout << "    redirect: " << loc.redirect << std::endl;
        }
    }

    std::cout << "----------------------------------" << std::endl;
}

void log_client_error(int status_code, const Connection* conn, const ServerConfig& config) {
    std::cerr << "Client error " << status_code << " for connection " 
              << conn->client_fd_ << " on ";
              
    if (!config.server_names_.empty()) {
        std::cerr << config.server_names_[0];
    } else {
        std::cerr << "default server";
    }
    
    std::cerr << ":" << config.port_;
    
    if (conn->request_data_ && !conn->request_data_->uri_.empty()) {
        std::cerr << " - URI: " << conn->request_data_->uri_;
    }
    
    std::cerr << std::endl;
}