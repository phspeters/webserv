#ifndef SERVER_MANAGER_HPP
#define SERVER_MANAGER_HPP

#include "webserv.hpp"

class ServerManager {
   public:
    // Need to initialize _epoll_fd, _running flag
    ServerManager();

    // Need to properly clean up servers and close epoll_fd
    ~ServerManager();

    // Initialize epoll and prepare for accepting servers
    bool init();

    void run();

    // Set the running flag to false and exit the event loop
    void shutdown();

    // Get pointer to singleton instance (for signal handler)
    static ServerManager* get_instance() { return instance_; };
    int get_epoll_fd() const { return epoll_fd_; }  // Get epoll fd
    Server* get_server_by_fd(int fd) const;

    bool register_server(Server* server);
    bool parse_config_file(
        const std::string& filename);  // Parses the config file and initializes
                                       // servers

    void unregister_fd(int fd);

    // Set up signal handlers for graceful shutdown (SIGINT, SIGTERM)
    static void setup_signal_handlers();

    // Signal handler callback (must be static)
    static void signal_handler(int signal);

   private:
    // Epoll management
    int epoll_fd_;
    std::vector<struct epoll_event> epoll_events_;
    static const int MAX_EPOLL_EVENTS = 1024;

    // Server management
    std::vector<Server*> servers_;  // List of servers managed by this manager
    std::map<int, Server*> fd_to_server_map_;  // Maps fd -> responsible server

    // Make singleton instance for signal handling
    static ServerManager* instance_;
    // State management
    volatile bool ready_;

    // Internal methods
    void event_loop();
    void cleanup_servers();
    bool check_server_health();
    void handle_error(const std::string& message);

    // Prevent copying
    ServerManager(const ServerManager&);
    ServerManager& operator=(const ServerManager&);
};

#endif  // SERVER_MANAGER_HPP