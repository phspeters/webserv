NAME = webserv

CC = c++
CFLAGS = -std=c++98 -Wall -Werror -Wextra
INCLUDES = -I include

VPATH = src
FILES = main.cpp \
		Connection.cpp \
		ConnectionManager.cpp \
		HttpRequest.cpp \
		RequestParser.cpp \
		Server.cpp \
		ServerConfig.cpp \
		ServerManager.cpp \
		FileUploadHandler.cpp \
		ResponseWriter.cpp \
		Router.cpp \
		StaticFileHandler.cpp \
		Logger.cpp \
		AHandler.cpp \

		
OBJS = $(FILES:%.cpp=$(OBJ_DIR)/%.o)
OBJ_DIR = obj

all: $(NAME)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@ && printf "Compiling: $(notdir $<)\n"

$(NAME): $(OBJS)
	@$(CC) $(CFLAGS) $(OBJS) -o $(NAME)

client:
	@echo "Compiling client..."
	@$(CC) $(CFLAGS) -o client tests/client.cpp

clean:
	@rm -rf $(OBJ_DIR)

fclean: clean
	@rm -rf $(NAME) client

re: fclean all

.PHONY: all clean fclean re