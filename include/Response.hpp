#ifndef RESPONSE_HPP
#define RESPONSE_HPP

#include <map>
#include <string>
#include <vector>

// Represents an HTTP Response to be sent back to the client.
// An instance of this is typically created by a handler
// and pointed to by Connection::response_data.
class Response {
   public:
    //--------------------------------------
    // Response Data Members
    //--------------------------------------
    int status_code;             // e.g., 200, 404
    std::string status_message;  // e.g., "OK", "Not Found"
    std::string version;  // e.g., "HTTP/1.1" (usually match request or default)

    std::map<std::string, std::string> headers;  // Response headers

    std::vector<char> body;  // Response body content (if generated in memory)

    // State flags (might be managed in Connection instead, depending on design)
    // bool        headers_built; // Indicate if headers string is ready
    // bool        headers_sent;
    // bool        body_sent;

    // Often useful to store these explicitly for header generation
    size_t content_length;
    std::string content_type;

    //--------------------------------------
    // Constructor / Destructor
    //--------------------------------------
    Response()
        : status_code(0),  // Indicate not set yet, perhaps? Or default to 200?
          content_length(0)
    // headers_built(false),
    // headers_sent(false),
    // body_sent(false)
    {
        // Initialize strings, maps, vectors to empty states automatically
        version = "HTTP/1.1";  // Default version
    }

    ~Response() {}

    //--------------------------------------
    // Helper Methods (declarations)
    //--------------------------------------
    void setHeader(const std::string& name, const std::string& value);
    void setStatus(int code, const std::string& message);

    // Method to generate the full status line + headers string (implementation
    // in .cpp)
    std::string getHeadersString() const;

    // Method to generate the status line only (implementation in .cpp)
    std::string getStatusLine() const;

    void clear() {
        status_code = 0;
        status_message.clear();
        version = "HTTP/1.1";
        headers.clear();
        body.clear();
        content_length = 0;
        content_type.clear();
        // headers_built = false;
        // headers_sent = false;
        // body_sent = false;
    }

   private:
    // Prevent copying if responses are managed by pointer in Connection
    Response(const Response&);
    Response& operator=(const Response&);

};  // class Response

#endif  // RESPONSE_HPP
