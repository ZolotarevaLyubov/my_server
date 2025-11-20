#ifndef SERVER_H
#define SERVER_H
#include <string>
#include <sys/socket.h>


class Server{
    int tcp_socket;
    int udp_socket;
    int epoll_fd;
    bool is_running;

    int total_clients;
    int current_clients;

    bool setup_sockets();
    bool setup_epoll();

    void handle_new_tcp_connection();
    void handle_tcp_data(int client_fd);
    void handle_tcp_disconnect(int client_fd);
    void handle_udp_data();
    void process_command(const std::string& cmd, int client_fd,
                         const sockaddr_storage* udp_addr = nullptr,
                         socklen_t udp_addr_len = 0);

    public:

    Server();
    ~Server();
    void run ();
    void stop();
    std::string get_stats();
        
};

#endif