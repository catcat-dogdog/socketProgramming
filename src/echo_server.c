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

#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include "parse.h"
#include "logger.h"

#define ECHO_PORT 9999
#define BUF_SIZE 4096

int close_socket(int sock)
{
    if (close(sock))
    {
        fprintf(stderr, "Failed closing socket.\n");
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int sock, client_sock;
    ssize_t readret;
    socklen_t cli_size;
    struct sockaddr_in addr, cli_addr;
    char buf[BUF_SIZE];

    fprintf(stdout, "----- Echo Server -----\n");

    // 初始化日志
    if (log_init("server.log") != 0)
    {
        fprintf(stderr, "Failed to initialize logger.\n");
        return EXIT_FAILURE;
    }

    LOG_INFO("Echo Server starting...");

    /* all networked programs must create a socket */
    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1)
    {
        LOG_ERROR("Failed creating socket");
        return EXIT_FAILURE;
    }

    // 添加 socket 重用选项
    int optval = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
    {
        LOG_ERROR("Failed to set socket options");
        close_socket(sock);
        return EXIT_FAILURE;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(ECHO_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    /* servers bind sockets to ports---notify the OS they accept connections */
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)))
    {
        LOG_ERROR("Failed binding socket: %s", strerror(errno));
        close_socket(sock); // 确保在错误时关闭 socket
        return EXIT_FAILURE;
    }

    if (listen(sock, 5))
    {
        close_socket(sock);
        fprintf(stderr, "Error listening on socket.\n");
        return EXIT_FAILURE;
    }

    /* finally, loop waiting for input and then write it back */
    while (1)
    {
        cli_size = sizeof(cli_addr);
        if ((client_sock = accept(sock, (struct sockaddr *)&cli_addr,
                                  &cli_size)) == -1)
        {
            LOG_ERROR("Error accepting connection");
            close(sock);
            return EXIT_FAILURE;
        }

        LOG_INFO("New client connected from %s:%d",
                 inet_ntoa(cli_addr.sin_addr),
                 ntohs(cli_addr.sin_port));

        readret = 0;
        memset(buf, 0, BUF_SIZE); // 清空缓冲区

        while ((readret = recv(client_sock, buf, BUF_SIZE, 0)) >= 1)
        {
            // ! JUST FOR TEST
            // int fd_in = open("./samples/test", O_RDONLY);
            // if (fd_in < 0)
            // {
            //     printf("Failed to open the file\n");
            //     return 0;
            // }
            // readret = read(fd_in, buf, BUF_SIZE);
            // close(fd_in);

            // 调用 parse() 函数解析请求
            Request *request = parse(buf, readret);
            if (request == NULL)
            {
                LOG_ERROR("Failed to parse request");
                char *http_response = "HTTP/1.1 400 Bad Request\r\n\r\n";
                send(client_sock, http_response, strlen(http_response), 0);
            }
            else
            {
                // 检查 request 的各个字段是否为 NULL
                if (request->http_method != NULL && request->http_uri != NULL && request->http_version != NULL)
                {
                    LOG_INFO("Received %s request for %s", request->http_method, request->http_uri);

                    if (strcmp(request->http_method, "GET") == 0 ||
                        strcmp(request->http_method, "HEAD") == 0 ||
                        strcmp(request->http_method, "POST") == 0)
                    {
                        char *http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n";
                        send(client_sock, http_response, strlen(http_response), 0);
                        send(client_sock, buf, readret, 0);
                    }
                    else
                    {
                        char *http_response = "HTTP/1.1 501 Not Implemented\r\n\r\n";
                        send(client_sock, http_response, strlen(http_response), 0);
                    }
                }
                else
                {
                    LOG_ERROR("Invalid request structure");
                    char *http_response = "HTTP/1.1 400 Bad Request\r\n\r\n";
                    send(client_sock, http_response, strlen(http_response), 0);
                }

                // 释放请求结构体
                if (request->headers != NULL)
                {
                    free(request->headers);
                }
                free(request);
            }

            memset(buf, 0, BUF_SIZE);
        }

        if (readret == -1)
        {
            LOG_ERROR("Error reading from client socket: %s", strerror(errno));
            close_socket(client_sock);
            close_socket(sock);
            fprintf(stderr, "Error reading from client socket.\n");
            return EXIT_FAILURE;
        }

        if (close_socket(client_sock))
        {
            close_socket(sock);
            LOG_INFO("Client disconnected");
            fprintf(stderr, "Error closing client socket.\n");
            return EXIT_FAILURE;
        }
    }

    close_socket(sock);

    log_close();
    return EXIT_SUCCESS;
}
