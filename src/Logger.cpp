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

void print_virtual_server(const VirtualServer& virtual_server) {
    std::cout << "---------- SERVER CONFIG ----------" << std::endl;
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

    std::cout << "----------------------------------" << std::endl;
}

void log_client_error(int status_code, const Connection* conn,
                      const VirtualServer& virtual_server) {
    std::cerr << "Client error " << status_code << " ("
              << HttpResponse::get_status_message(status_code)
              << ") for connection " << conn->client_fd_ << " on ";

    if (!virtual_server.server_names_.empty()) {
        std::cerr << virtual_server.server_names_[0];
    } else {
        std::cerr << "default server";
    }

    std::cerr << ":" << virtual_server.port_;

    if (conn->request_data_ && !conn->request_data_->uri_.empty()) {
        std::cerr << " - URI: " << conn->request_data_->uri_;
    }

    std::cerr << std::endl;
}

void build_mock_response(Connection* conn) {
    HttpResponse* mock_response = conn->response_data_;
    if (mock_response == NULL) {
        std::cerr << "Error: response_data_ is NULL" << std::endl;
        return;
    }

    mock_response->status_code_ = 200;
    mock_response->status_message_ = "OK";
    mock_response->headers_["Content-Type"] = "text/plain";
    mock_response->headers_["Content-Length"] = "13";
    std::string hello_str = "Hello, World!";
    mock_response->body_.assign(hello_str.begin(), hello_str.end());
    mock_response->headers_["Connection"] = "close";
    mock_response->headers_["Date"] = "Wed, 21 Oct 2015 07:28:00 GMT";
    mock_response->headers_["Server"] = "webserv/1.0";
    mock_response->headers_["Last-Modified"] = "Wed, 21 Oct 2015 07:28:00 GMT";
    mock_response->headers_["Content-Language"] = "en-US";
    mock_response->headers_["Authorization"] = "Basic dXNlcm5hbWU6cGFzc3dvcmQ=";
    mock_response->headers_["Cookie"] = "sessionId=abc123";
    mock_response->headers_["Host"] = "localhost:8080";
}

std::string get_current_gmt_time() {
    char buffer[100];
    time_t now = time(NULL);
    struct tm* tm_info = gmtime(&now);

    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S: ", tm_info);
    return std::string(buffer);
}

int log(log_level level, const char* msg, ...) {
    if (level < ACTIVE_LOG_LEVEL) {
		return 0;
	}

	char output[8192];
    va_list args;
    int n;

    va_start(args, msg);
    n = vsnprintf(output, 8192, msg, args);

    if (level == LOG_DEBUG)
        std::cout << WHITE << "[DEBUG]\t";
    else if (level == LOG_INFO)
        std::cout << CYAN << "[INFO]\t";
	else if (level == LOG_WARNING)
		std::cout << MAGENTA << "[WARNING]\t";
    else if (level == LOG_ERROR)
        std::cout << RED << "[ERROR]\t";

    std::cout << get_current_gmt_time() << output << RESET << std::endl;
    va_end(args);

	return n;
}
