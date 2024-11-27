/******************************************************************************
 * echo_server.c                                                               *
 *                                                                             *
 * Description: This file contains the C source code for an echo server.  The  *
 *              server runs on a hard-coded port and simply write back anything*
 *              sent to it by connected clients.  It does not support          *
 *              concurrent clients.                                            *
 *                                                                             *
 * Authors: Athula Balachandran <abalacha@cs.cmu.edu>,                         *
 *          Wolf Richter <wolf@cs.cmu.edu>                                     *
 *                                                                             *
 *******************************************************************************/

#include "echo_server.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#define ECHO_PORT 9999
#define MAX_CLIENTS 1024 // 最大客户端连接数
#define TIMEOUT_SECS 5   // select超时时间(秒)

static int close_socket(int sock)
{
    if (close(sock))
    {
        LOG_ERROR("Failed closing socket: %s", strerror(errno));
        return 1;
    }
    return 0;
}

int server_init(server_t *server) {
    // 初始化服务器结构
    memset(server, 0, sizeof(server_t));
    
    // 创建socket
    if ((server->server_sock = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
        LOG_ERROR("Failed creating socket");
        return -1;
    }

    // 设置socket选项
    int optval = 1;
    if (setsockopt(server->server_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        LOG_ERROR("Failed to set socket options");
        close_socket(server->server_sock);
        return -1;
    }

    // 配置服务器地址
    server->server_addr.sin_family = AF_INET;
    server->server_addr.sin_port = htons(ECHO_PORT);
    server->server_addr.sin_addr.s_addr = INADDR_ANY;

    // 绑定地址
    if (bind(server->server_sock, (struct sockaddr *)&server->server_addr, sizeof(server->server_addr))) {
        LOG_ERROR("Failed binding socket: %s", strerror(errno));
        close_socket(server->server_sock);
        return -1;
    }

    // 监听连接
    if (listen(server->server_sock, 5)) {
        LOG_ERROR("Error listening on socket: %s", strerror(errno));
        close_socket(server->server_sock);
        return -1;
    }

    // 设置非阻塞
    int flags = fcntl(server->server_sock, F_GETFL, 0);
    fcntl(server->server_sock, F_SETFL, flags | O_NONBLOCK);

    server->is_running = 1;
    return 0;
}

// 主循环逻辑移到单独的函数中
int server_run(server_t *server) {
    while (server->is_running) {
                fd_set read_fds;
        struct timeval tv;
        int max_fd = server->server_sock;

        FD_ZERO(&read_fds);
        FD_SET(server->server_sock, &read_fds);

        // 添加所有活跃的客户端到fd_set
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (server->clients[i].sockfd > 0)
            {
                FD_SET(server->clients[i].sockfd, &read_fds);
                if (server->clients[i].sockfd > max_fd)
                {
                    max_fd = server->clients[i].sockfd;
                }
            }
        }

        // 设置select超时
        tv.tv_sec = TIMEOUT_SECS;
        tv.tv_usec = 0;

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

        if (activity < 0)
        {
            LOG_ERROR("Select error: %s", strerror(errno));
            continue;
        }

        // 处理新连接
        if (FD_ISSET(server->server_sock, &read_fds))
        {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int client_sock = accept(server->server_sock, (struct sockaddr *)&client_addr, &addr_len);

            if (client_sock < 0)
            {
                LOG_ERROR("Accept failed: %s", strerror(errno));
                continue;
            }

            // 设置新socket为非阻塞
            int flags = fcntl(client_sock, F_GETFL, 0);
            fcntl(client_sock, F_SETFL, flags | O_NONBLOCK);

            // 获取客户端IP地址
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

            // 查找空闲的客户端槽位
            int i;
            for (i = 0; i < MAX_CLIENTS; i++)
            {
                if (server->clients[i].sockfd == 0)
                {
                    client_init(&server->clients[i], client_sock, client_addr, BUF_SIZE);

                    LOG_INFO("New client connected - IP: %s, Port: %d, Socket: %d, Slot: %d",
                             client_ip,
                             ntohs(client_addr.sin_port),
                             client_sock,
                             i);
                    break;
                }
            }

            if (i == MAX_CLIENTS)
            {
                LOG_ERROR("Connection rejected - Too many connections (Max: %d) from IP: %s, Port: %d",
                          MAX_CLIENTS,
                          client_ip,
                          ntohs(client_addr.sin_port));
                close(client_sock);
            }
        }

        // 处理客户端数据
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (server->clients[i].sockfd > 0 && FD_ISSET(server->clients[i].sockfd, &read_fds))
            {
                client_handle(&server->clients[i]);
            }
        }

        // 检查超时连接
        time_t current_time = time(NULL);
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (server->clients[i].sockfd > 0 &&
                (current_time - server->clients[i].last_active) > TIMEOUT_SECS)
            {
                close(server->clients[i].sockfd);
                server->clients[i].sockfd = 0;
            }
        }
    }
    return 0;
}

void server_cleanup(server_t *server) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i].sockfd > 0) {
            client_destroy(&server->clients[i]);
        }
    }
    close_socket(server->server_sock);
}

int main(int argc, char *argv[]) {
    server_t server;
    
    fprintf(stdout, "----- Echo Server -----\n");

    // 初始化日志
    if (log_init("server.log") != 0) {
        fprintf(stderr, "Failed to initialize logger.\n");
        return EXIT_FAILURE;
    }

    LOG_INFO("Echo Server starting...");

    // 初始化服务器
    if (server_init(&server) != 0) {
        log_close();
        return EXIT_FAILURE;
    }

    // 运行服务器
    int result = server_run(&server);

    // 清理资源
    server_cleanup(&server);
    log_close();
    
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}