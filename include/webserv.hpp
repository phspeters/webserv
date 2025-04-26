#ifndef WEBSERV_HPP
#define WEBSERV_HPP

#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "IHandler.hpp"
#include "ServerConfig.hpp"

#include "CgiHandler.hpp"
#include "Connection.hpp"
#include "ConnectionManager.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "RequestParser.hpp"
#include "ResponseWriter.hpp"
#include "Router.hpp"
#include "RouteConfig.hpp"
#include "ServerBlock.hpp"
#include "Server.hpp"
#include "ServerManager.hpp"
#include "StaticFileHandler.hpp"

#endif