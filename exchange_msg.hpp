#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <unistd.h>

#include "debug.hpp"

// Exchange message with a tcp connection
void exchange_msg(bool as_server, const char *server_addr,
                  const char *server_port, void *msg, size_t msg_len);

// Exchange message with a tcp connection
// server_addr and server_port should be in network byte order
void exchange_msg(bool as_server, in_addr_t server_addr, in_port_t server_port,
                  void *msg, size_t msg_len);

inline void exchange_msg(bool as_server, const char *server_addr,
                         const char *server_port, void *msg, size_t msg_len) {
    in_addr_t addr = inet_addr(server_addr);
    ASSERT(addr != -1);
    in_port_t port = atoi(server_port);
    ASSERT(port != 0);
    exchange_msg(as_server, addr, htons(port), msg, msg_len);
}

inline void exchange_msg(int conn_fd, void *msg, size_t msg_len) {
    ssize_t sent_size = send(conn_fd, msg, msg_len, 0);
    ASSERT(sent_size == msg_len);
    ssize_t recv_size = recv(conn_fd, msg, msg_len, 0);
    ASSERT(recv_size == msg_len);
}

inline void exchange_msg(bool as_server, in_addr_t server_addr,
                         in_port_t server_port, void *msg, size_t msg_len) {
    sockaddr_in sock_addr = {.sin_family = AF_INET,
                             .sin_port = server_port,
                             .sin_addr = {.s_addr = server_addr}};
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT(sock_fd);

    if (as_server) {
        CHECK(bind(sock_fd, reinterpret_cast<sockaddr *>(&sock_addr),
                   sizeof(sock_addr)));

        CHECK(listen(sock_fd, 1));

        sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int conn_fd =
            accept(sock_fd, reinterpret_cast<sockaddr *>(&client_addr),
                   &client_addr_len);
        ASSERT(conn_fd);
        ASSERT(client_addr_len == sizeof(client_addr));

        exchange_msg(conn_fd, msg, msg_len);

        CHECK(close(conn_fd));
    } else {
        CHECK(connect(sock_fd, reinterpret_cast<sockaddr *>(&sock_addr),
                      sizeof(sock_addr)));
        exchange_msg(sock_fd, msg, msg_len);
    }

    CHECK(close(sock_fd));
}
