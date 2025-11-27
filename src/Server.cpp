#include "Server.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <cstring>
#include <iostream>
#include <string>
#include <algorithm>
#include <sstream>
#include <ctime>


bool Server::setup_sockets() {
    tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(tcp_socket == -1) {
        std::cerr << "Error: creating TCP socket\n";
        return false;
    }
    
    int flags = fcntl(tcp_socket, F_GETFL, 0);
    fcntl(tcp_socket, F_SETFL, flags | O_NONBLOCK);

    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if(udp_socket == -1) {
        std::cerr << "Error: creating UDP socket\n";
        return false;
    }

    flags = fcntl(udp_socket, F_GETFL, 0);
    fcntl(udp_socket, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(12345);

    if (bind(tcp_socket, (sockaddr*)&addr, sizeof(addr)) == -1) {
        std::cerr << "Error: binding TCP socket\n";
        close(tcp_socket);
        close(udp_socket);
        return false;
    }

    if (listen(tcp_socket, SOMAXCONN) == -1) {
        std::cerr << "Error: listening on TCP socket\n";
        close(tcp_socket);
        close(udp_socket);
        return false;
    }

     if (bind(udp_socket, (sockaddr*)&addr, sizeof(addr)) == -1) {
        std::cerr << "Error: binding UDP socket\n";
        close(tcp_socket);
        close(udp_socket);
        return false;
    }

    return true;
}


bool Server::setup_epoll() {
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        std::cerr << "Error: creating epoll\n";
        return false;
    }

    epoll_event event;

    event.events = EPOLLIN;
    event.data.fd = tcp_socket;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcp_socket, &event) == -1) {
        std::cerr << "Error: adding TCP socket to epoll\n";
        close(epoll_fd);
        return false;
    }

    event.events = EPOLLIN;
    event.data.fd = udp_socket;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, udp_socket, &event) == -1) {
        std::cerr << "Error: adding UDP socket to epoll\n";
        close(epoll_fd);
        return false;
    }
    return true;
}

void Server::handle_new_tcp_connection() {
    int client_fd = accept(tcp_socket, nullptr, nullptr);
    if(client_fd == -1) 
        return;

    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    
    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = client_fd;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);

    total_clients++;
    current_clients++;
}

void Server::handle_tcp_data(int client_fd) {
    char buffer[1024];

    int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_read <= 0) {
        if (bytes_read == 0) {
            handle_tcp_disconnect(client_fd);
        }
        return;
    }

    buffer[bytes_read] = '\0';
    std::string message = buffer;

    if(!message.empty() && message[0] == '/') {
        process_command(message, client_fd);
        return;
    }

    send(client_fd, message.c_str(), message.size(), 0);
}

void Server::handle_tcp_disconnect(int client_fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
    current_clients = std::max(0, current_clients - 1);
}

void Server::handle_udp_data() {
    char buffer[2048];
    sockaddr_storage src_addr;
    socklen_t src_len = sizeof(src_addr);

    ssize_t bytes = recvfrom(udp_socket, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr*>(&src_addr), &src_len);
    if (bytes == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) 
            return;
        std::cerr << "Error: recvfrom UDP: " << strerror(errno) << "\n";
        return;    
    }

    buffer[bytes] = '\0';
    std::string message(buffer);

    if(!message.empty() && message[0] == '/') {
        process_command(message, -1, &src_addr, src_len);
        return;
    }

    ssize_t sent = sendto(udp_socket, message.c_str(), message.size(), 0, reinterpret_cast<sockaddr*>(&src_addr), src_len);
}

void Server::process_command(const std::string& cmd, int client_fd,
                         const sockaddr_storage* udp_addr,
                         socklen_t udp_addr_len) {
    if (cmd == "/time" || cmd.rfind("/time", 0) == 0) {
        std::time_t t = std::time(nullptr);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        std::string resp(buf);

        if (client_fd != -1) {
            send(client_fd, resp.c_str(), resp.size(), 0);
        } else if (udp_addr) {
            sendto(udp_socket, resp.c_str(), resp.size(), 0, 
                   reinterpret_cast<const sockaddr*>(udp_addr), udp_addr_len);
        }
        return;
    }

    if (cmd == "/stats" || cmd.rfind("/stats", 0) == 0) {
        std::ostringstream ss;
        ss << "total_clients=" << total_clients << " current_clients=" <<current_clients;
        std::string resp = ss.str();

        if (client_fd != -1) {
            send(client_fd, resp.c_str(), resp.size(), 0);
        } else if (udp_addr) {
            sendto(udp_socket, resp.c_str(), resp.size(), 0, 
                   reinterpret_cast<const sockaddr*>(udp_addr), udp_addr_len);
        }
        return;
    }

    if (cmd == "/shutdown" || cmd.rfind("/shutdown", 0) == 0) {
        std::string resp = "shutting down";
        if(client_fd != -1){
            send(client_fd, resp.c_str(), resp.size(), 0);
        } else if (udp_addr) {
            sendto(udp_socket, resp.c_str(), resp.size(), 0, 
                   reinterpret_cast<const sockaddr*>(udp_addr), udp_addr_len);
        }
        stop();
        return;
    }

    std::string unknown = "unknown command";
    if(client_fd != -1){
            send(client_fd, unknown.c_str(), unknown.size(), 0);
        } else if (udp_addr) {
            sendto(udp_socket, unknown.c_str(), unknown.size(), 0, 
                   reinterpret_cast<const sockaddr*>(udp_addr), udp_addr_len);
        }     
                            
}


Server::Server() : tcp_socket(-1), 
                   udp_socket(-1),
                   epoll_fd(-1),
                   is_running(true),
                   total_clients(0),
                   current_clients(0) {}

Server::~Server() {
    if (tcp_socket != -1) {
        close(tcp_socket);
        tcp_socket = -1;
    }

    if (udp_socket != -1) {
        close(udp_socket);
        udp_socket = -1;
    }

    if (epoll_fd != -1) {
        close(epoll_fd);
        epoll_fd = -1;
    }
}
void Server::run () {
    if (!setup_sockets()) {
        std::cerr << "setup sockets failed\n";
        return;
    }

    if (!setup_epoll()) {
        std::cerr << "setup epoll failed\n";
        return;
    }

    const int MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS];

    is_running = true;

    while (is_running) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n == -1) {
            if (errno == EINTR) continue;
            std::cerr << "epoll_wait error: " << strerror(errno) << "\n";
            break;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            if(fd == tcp_socket) {
                handle_new_tcp_connection();
                continue;
            }

            if(fd == udp_socket) {
                handle_udp_data();
                continue;
            }
            if (events[i].events & EPOLLIN) {
                handle_tcp_data(fd);
            } else {
                handle_tcp_disconnect(fd);
            }
        }
    }

    if (epoll_fd != -1) { close(epoll_fd); epoll_fd = -1; }
    if (tcp_socket != -1) { close(tcp_socket); tcp_socket = -1; }
    if (udp_socket != -1) { close(udp_socket); udp_socket = -1; }
}

void Server::stop() {
    is_running = false;
}

std::string Server::get_stats() {
    std::ostringstream ss;
    ss << "total_clients = " << total_clients << " current_clients = " << current_clients;
    return ss.str();
}