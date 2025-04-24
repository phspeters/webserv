int main(int argc, char* argv[]) {

}


/*
Core Server & Management: Server.hpp, ServerConfig.hpp, ConnectionManager.hpp, Connection.hpp define the main server loop, configuration handling, connection lifecycle, and individual connection state.
HTTP Handling: Request.hpp, Response.hpp, RequestParser.hpp, ResponseWriter.hpp define how HTTP requests and responses are represented, parsed, and formatted.
Request Processing Logic: IHandler.hpp (base class), StaticFileHandler.hpp, CgiHandler.hpp, and Router.hpp define the request handling abstraction, specific handlers for static files and CGI, and the mechanism to choose the correct handler.
*/