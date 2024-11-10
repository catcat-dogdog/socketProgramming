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
#include "parse.h"

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

    /* all networked programs must create a socket */
    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1)
    {
        fprintf(stderr, "Failed creating socket.\n");
        return EXIT_FAILURE;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(ECHO_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    /* servers bind sockets to ports---notify the OS they accept connections */
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)))
    {
        close_socket(sock);
        fprintf(stderr, "Failed binding socket.\n");
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
            close(sock);
            fprintf(stderr, "Error accepting connection.\n");
            return EXIT_FAILURE;
        }

        readret = 0;

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
                // 格式错误，返回 400 Bad Request
                char *http_response = "HTTP/1.1 400 Bad Request\r\n\r\n";
                send(client_sock, http_response, strlen(http_response), 0);
            }
            else
            {
                // 根据请求方法返回响应
                if (strcmp(request->http_method, "GET") == 0 || strcmp(request->http_method, "HEAD") == 0 || strcmp(request->http_method, "POST") == 0)
                {
                    // Echo 请求内容
                    char *http_response = "HTTP/1.1 200 OK\r\n\r\n";
                    send(client_sock, http_response, strlen(http_response), 0);
                }
                else
                {
                    // 返回 501 Not Implemented 错误
                    char *http_response = "HTTP/1.1 501 Not Implemented\r\n\r\n";
                    send(client_sock, http_response, strlen(http_response), 0);
                }

                // 释放内存
                free(request->headers);
                free(request);
            }

            ssize_t sent = send(client_sock, buf, readret, 0);
            if (sent != readret)
            {
                close_socket(client_sock);
                close_socket(sock);
                fprintf(stderr, "Error sending to client.\n");
                return EXIT_FAILURE;
            }
            memset(buf, 0, BUF_SIZE);
        }

        if (readret == -1)
        {
            close_socket(client_sock);
            close_socket(sock);
            fprintf(stderr, "Error reading from client socket.\n");
            return EXIT_FAILURE;
        }

        if (close_socket(client_sock))
        {
            close_socket(sock);
            fprintf(stderr, "Error closing client socket.\n");
            return EXIT_FAILURE;
        }
    }

    close_socket(sock);

    return EXIT_SUCCESS;
}
