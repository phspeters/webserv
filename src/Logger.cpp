#include "common.hpp"

int log(log_level level, const char* msg, ...) {
    if (level < ACTIVE_LOG_LEVEL) {
        return 0;
    }

    char output[8192];
    va_list args;
    int n;

    va_start(args, msg);
    n = vsnprintf(output, 8192, msg, args);

    std::string timestamp = get_current_gmt_time();

    if (level == LOG_TRACE)
        std::cerr << LIGHT_GREY << "[TRACE]\t";
    else if (level == LOG_DEBUG)
        std::cerr << WHITE << "[DEBUG]\t";
    else if (level == LOG_INFO)
        std::cerr << CYAN << "[INFO]\t";
    else if (level == LOG_WARNING)
        std::cerr << MAGENTA << "[WARN]\t";
    else if (level == LOG_ERROR)
        std::cerr << LIGHT_RED << "[ERROR]\t";
    else if (level == LOG_FATAL)
        std::cerr << RED << "[FATAL]\t";
    else if (level == LOG_OFF)
        return 0;  // No output for LOG_OFF

    std::cerr << timestamp << output << RESET << std::endl;
    va_end(args);

    return n;
}

void log_request(log_level level, const Connection* conn) {
    if (level < ACTIVE_LOG_LEVEL) {
        return;  // No output for lower log levels
    }

    if (conn == NULL) {
        std::cerr << "Error: connection is NULL" << std::endl;
        return;
    }

    std::string timestamp = get_current_gmt_time();

    if (level == LOG_TRACE)
        std::cerr << LIGHT_GREY << "[TRACE]\t";
    else if (level == LOG_DEBUG)
        std::cerr << WHITE << "[DEBUG]\t";
    else if (level == LOG_INFO)
        std::cerr << CYAN << "[INFO]\t";
    else if (level == LOG_WARNING)
        std::cerr << MAGENTA << "[WARN]\t";
    else if (level == LOG_ERROR)
        std::cerr << LIGHT_RED << "[ERROR]\t";
    else if (level == LOG_FATAL)
        std::cerr << RED << "[FATAL]\t";
    else if (level == LOG_OFF)
        return;  // No output for LOG_OFF

    std::cerr << timestamp;

    std::cerr << "\n==== INCOMING REQUEST (fd: " << conn->client_fd_
              << ") ====\n";
    std::cerr << "method: " << conn->request_data_->method_ << std::endl;
    std::cerr << "uri: " << conn->request_data_->uri_ << std::endl;
    std::cerr << "version: " << conn->request_data_->version_ << std::endl;
    std::cerr << "headers: " << std::endl;
    for (std::map<std::string, std::string>::const_iterator it =
             conn->request_data_->headers_.begin();
         it != conn->request_data_->headers_.end(); ++it) {
        std::cerr << "  " << it->first << ": " << it->second << std::endl;
    }
    std::cerr << "body: " << std::endl;
    std::cerr.write(conn->request_data_->body_buffer_.data(),
                    conn->request_data_->body_buffer_.readable_bytes());
    std::cerr << "====================================" << RESET << std::endl;
}

void log_response(log_level level, Connection* conn) {
    if (level < ACTIVE_LOG_LEVEL) {
        return;  // No output for lower log levels
    }

    if (conn == NULL) {
        std::cerr << "Error: connection is NULL" << std::endl;
        return;
    }

    std::string timestamp = get_current_gmt_time();

    if (level == LOG_TRACE)
        std::cerr << LIGHT_GREY << "[TRACE]\t";
    else if (level == LOG_DEBUG)
        std::cerr << WHITE << "[DEBUG]\t";
    else if (level == LOG_INFO)
        std::cerr << CYAN << "[INFO]\t";
    else if (level == LOG_WARNING)
        std::cerr << MAGENTA << "[WARN]\t";
    else if (level == LOG_ERROR)
        std::cerr << LIGHT_RED << "[ERROR]\t";
    else if (level == LOG_FATAL)
        std::cerr << RED << "[FATAL]\t";
    else if (level == LOG_OFF)
        return;  // No output for LOG_OFF

    std::cerr << timestamp;

    std::cerr << "\n==== HTTP RESPONSE ====\n";
    std::cerr << "Status: " << conn->response_data_->status_code_ << " "
              << conn->response_data_->status_message_ << std::endl;
    std::cerr << "Headers: ";
    for (std::map<std::string, std::string>::const_iterator it =
             conn->response_data_->headers_.begin();
         it != conn->response_data_->headers_.end(); ++it) {
        std::cerr << it->first << "=" << it->second << "; ";
    }
    std::cerr << std::endl;
    std::cerr << "Body size: " << conn->response_data_->body_data_.size()
              << " bytes" << std::endl;
    std::cerr << "---------------------" << std::endl;
    std::cerr.write(conn->response_data_->body_data_.data(),
                    conn->response_data_->body_data_.size());
    std::cerr << "=====================" << RESET << std::endl;
}

int log_buffer(log_level level, const Buffer& buffer) {
    if (level < ACTIVE_LOG_LEVEL) {
        return 0;
    }

    std::string timestamp = get_current_gmt_time();

    if (level == LOG_TRACE)
        std::cerr << LIGHT_GREY << "[TRACE]\t";
    else if (level == LOG_DEBUG)
        std::cerr << WHITE << "[DEBUG]\t";
    else if (level == LOG_INFO)
        std::cerr << CYAN << "[INFO]\t";
    else if (level == LOG_WARNING)
        std::cerr << MAGENTA << "[WARN]\t";
    else if (level == LOG_ERROR)
        std::cerr << LIGHT_RED << "[ERROR]\t";
    else if (level == LOG_FATAL)
        std::cerr << RED << "[FATAL]\t";
    else if (level == LOG_OFF)
        return 0;  // No output for LOG_OFF

    std::cerr << timestamp;

    std::cerr << "\n========== BUFFER START ==========\n";
    int bytes_written = write(1, buffer.data(), buffer.readable_bytes());
    std::cerr << "=========== BUFFER END ===========";

    if (bytes_written < 0) {
        std::cerr << "Error writing to buffer" << std::endl;
        return -1;
    }

    if (bytes_written == 0) {
        std::cerr << "Buffer is empty" << std::endl;
    }

    std::cerr << RESET << std::endl;

    return bytes_written;
}

void log_virtual_server(log_level level, const VirtualServer& virtual_server) {
    if (level < ACTIVE_LOG_LEVEL) {
        return;  // No output for lower log levels
    }

    std::string timestamp = get_current_gmt_time();

    if (level == LOG_TRACE)
        std::cerr << LIGHT_GREY << "[TRACE]\t";
    else if (level == LOG_DEBUG)
        std::cerr << WHITE << "[DEBUG]\t";
    else if (level == LOG_INFO)
        std::cerr << CYAN << "[INFO]\t";
    else if (level == LOG_WARNING)
        std::cerr << MAGENTA << "[WARN]\t";
    else if (level == LOG_ERROR)
        std::cerr << LIGHT_RED << "[ERROR]\t";
    else if (level == LOG_FATAL)
        std::cerr << RED << "[FATAL]\t";
    else if (level == LOG_OFF)
        return;  // No output for LOG_OFF

    std::cerr << timestamp;

    std::cout << "\n=========== VIRTUAL SERVER ==========" << std::endl;
    std::cout << "Host: " << virtual_server.host_ << std::endl;
    std::cout << "Port: " << virtual_server.port_ << std::endl;

    // Print server names
    std::cout << "Server Names: ";
    if (virtual_server.server_names_.empty()) {
        std::cout << "(default server)";
    } else {
        for (size_t i = 0; i < virtual_server.server_names_.size(); ++i) {
            std::cout << virtual_server.server_names_[i];
            if (i < virtual_server.server_names_.size() - 1) {
                std::cout << ", ";
            }
        }
    }
    std::cout << std::endl;

    // Print client max body size with unit
    std::cout << "Client Max Body Size: ";
    if (virtual_server.client_max_body_size_ >= 1024 * 1024 * 1024) {
        std::cout << (virtual_server.client_max_body_size_ /
                      (1024 * 1024 * 1024))
                  << "G";
    } else if (virtual_server.client_max_body_size_ >= 1024 * 1024) {
        std::cout << (virtual_server.client_max_body_size_ / (1024 * 1024))
                  << "M";
    } else if (virtual_server.client_max_body_size_ >= 1024) {
        std::cout << (virtual_server.client_max_body_size_ / 1024) << "K";
    } else {
        std::cout << virtual_server.client_max_body_size_ << " bytes";
    }
    std::cout << std::endl;

    // Print error pages
    std::cout << "Error Pages:" << std::endl;
    if (virtual_server.error_pages_.empty()) {
        std::cout << "  (none)" << std::endl;
    } else {
        for (std::map<int, std::string>::const_iterator it =
                 virtual_server.error_pages_.begin();
             it != virtual_server.error_pages_.end(); ++it) {
            std::cout << "  " << it->first << " -> " << it->second << std::endl;
        }
    }

    // Print location blocks
    std::cout << "Location Blocks (" << virtual_server.locations_.size()
              << "):" << std::endl;
    for (size_t i = 0; i < virtual_server.locations_.size(); ++i) {
        const Location& loc = virtual_server.locations_[i];
        std::cout << "  ---------- LOCATION: " << loc.path_ << " ----------"
                  << std::endl;

        std::cout << "    root: " << loc.root_ << std::endl;

        std::cout << "    autoindex: " << (loc.autoindex_ ? "on" : "off")
                  << std::endl;

        std::cout << "    allowed_methods: ";
        for (size_t j = 0; j < loc.allowed_methods_.size(); ++j) {
            std::cout << loc.allowed_methods_[j];
            if (j < loc.allowed_methods_.size() - 1) {
                std::cout << ", ";
            }
        }
        std::cout << std::endl;

        std::cout << "    cgi: " << (loc.cgi_enabled_ ? "on" : "off")
                  << std::endl;

        std::cout << "    index: " << loc.index_ << std::endl;

        if (!loc.redirect_.empty()) {
            std::cout << "    redirect: " << loc.redirect_ << std::endl;
        }
    }

    std::cout << "================================" << RESET << std::endl;
}

const char* event_to_string(uint32_t event_flags) {
    switch (event_flags) {
        case EPOLLIN:
            return "EPOLLIN";
        case EPOLLOUT:
            return "EPOLLOUT";
        case EPOLLIN | EPOLLOUT:
            return "EPOLLIN | EPOLLOUT";
        case EPOLLERR:
            return "EPOLLERR";
        case EPOLLHUP:
            return "EPOLLHUP";
        case EPOLLRDHUP:
            return "EPOLLRDHUP";
        default:
            return "UNKNOWN EVENT";
    }
}
