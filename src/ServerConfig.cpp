#include "ServerConfig.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

// Constructor: Initializes default values and MIME types
ServerConfig::ServerConfig()
    : port(8080),
      server_root("./www"),
      default_index("index.html"),
      max_request_body_size(1024 * 1024),  // 1 MB
      connection_timeout_seconds(30),
      cgi_default_handler("/usr/bin/php-cgi") {
    setDefaultMimeTypes();
}

// Sets up common MIME types that the server should recognize
// Determines the correct Content-Type HTTP header the browser should interpret 
void ServerConfig::setDefaultMimeTypes() {
    // Text formats
    mime_types[".html"] = "text/html";
    mime_types[".htm"] = "text/html";
    mime_types[".css"] = "text/css";
    mime_types[".txt"] = "text/plain";
    mime_types[".js"] = "application/javascript";
    mime_types[".json"] = "application/json";
    mime_types[".xml"] = "application/xml";
    
    // Image formats
    mime_types[".jpg"] = "image/jpeg";
    mime_types[".jpeg"] = "image/jpeg";
    mime_types[".png"] = "image/png";
    mime_types[".gif"] = "image/gif";
    mime_types[".svg"] = "image/svg+xml";
    mime_types[".ico"] = "image/x-icon";
    
    // Application formats
    mime_types[".pdf"] = "application/pdf";
    mime_types[".zip"] = "application/zip";
    
    // Audio/Video formats
    mime_types[".mp3"] = "audio/mpeg";
    mime_types[".mp4"] = "video/mp4";
    mime_types[".webm"] = "video/webm";
}

// Get MIME type based on filename extension
std::string ServerConfig::getMimeType(const std::string& filename) const {
  size_t dotPos = filename.find_last_of('.');
  if (dotPos == std::string::npos) {
      return "application/octet-stream";  // Default binary type
  }

  std::string extension = to_lower(filename.substr(dotPos));
  std::map<std::string, std::string>::const_iterator it =
      mime_types.find(extension);

  if (it != mime_types.end()) {
      return it->second;  // Found specific type
  } else {
      return "application/octet-stream";  // Default if extension unknown
  }
}

// Load configuration from a file
bool ServerConfig::loadFromFile(const std::string& filename) {
  std::ifstream configFile(filename);
  if (!configFile.is_open()) {
      std::cerr << "Failed to open config file: " << filename << std::endl;
      return false;
  }
  
  std::string line;
  std::string section;
  
  while (std::getline(configFile, line)) {
      // Skip empty lines and comments
      if (line.empty() || line[0] == '#' || line[0] == ';') {
          continue;
      }
      
      // Trim leading/trailing whitespace
      line.erase(0, line.find_first_not_of(" \t"));
      line.erase(line.find_last_not_of(" \t") + 1);
      
      // Check for section header [section]
      if (line[0] == '[' && line[line.length() - 1] == ']') {
          section = line.substr(1, line.length() - 2);
          continue;
      }
      
      // Find key-value separator
      size_t separatorPos = line.find('=');
      if (separatorPos == std::string::npos) {
          continue;  // Invalid line format, skip
      }
      
      std::string key = line.substr(0, separatorPos);
      std::string value = line.substr(separatorPos + 1);
      
      // Trim key and value
      key.erase(0, key.find_first_not_of(" \t"));
      key.erase(key.find_last_not_of(" \t") + 1);
      value.erase(0, value.find_first_not_of(" \t"));
      value.erase(value.find_last_not_of(" \t") + 1);
      
      // Process the key-value pair based on section
      if (section == "server") {
          if (key == "port") {
              port = static_cast<unsigned short>(std::stoi(value));
          } else if (key == "server_root") {
              server_root = value;
          } else if (key == "default_index") {
              default_index = value;
          } else if (key == "max_request_body_size") {
              max_request_body_size = std::stoull(value);
          } else if (key == "connection_timeout_seconds") {
              connection_timeout_seconds = std::stoi(value);
          }
      } else if (section == "cgi") {
          if (key == "default_handler") {
              cgi_default_handler = value;
          } else {
              // Assume it's a CGI mapping
              cgi_executables[key] = value;
          }
      } else if (section == "mime") {
          mime_types[key] = value;
      }
  }
  
  configFile.close();
  return true;
}