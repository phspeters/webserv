#ifndef WEBSERV_HPP
#define WEBSERV_HPP

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <set> 
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "IHandler.hpp"
#include "ServerConfig.hpp"
#include "RequestParser.hpp"

#include "CgiHandler.hpp"
#include "Connection.hpp"
#include "ConnectionManager.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "ResponseWriter.hpp"
#include "Router.hpp"
#include "Server.hpp"
#include "ServerManager.hpp"
#include "StaticFileHandler.hpp"
#include "ServerConfig.hpp"
#include "FileUploadHandler.hpp"

// utils
std::string trim(const std::string& str);

#endif