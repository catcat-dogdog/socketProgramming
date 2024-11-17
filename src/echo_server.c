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
#include <sys/stat.h>
#include <time.h>

#define ECHO_PORT 9999
#define BUF_SIZE 4096

// 响应状态码定义
#define RESPONSE_200 "HTTP/1.1 200 OK\r\n"
#define RESPONSE_400 "HTTP/1.1 400 Bad Request\r\n\r\n"
#define RESPONSE_404 "HTTP/1.1 404 Not Found\r\n\r\n"
#define RESPONSE_500 "HTTP/1.1 500 Internal Server Error\r\n\r\n"
#define RESPONSE_501 "HTTP/1.1 501 Not Implemented\r\n\r\n"
#define RESPONSE_505 "HTTP/1.1 505 HTTP Version Not Supported\r\n\r\n"

// 添加在文件开头的全局变量部分
struct mime_type {
    const char *extension;
    const char *type;
};

static const struct mime_type mime_types[] = {
    {".html", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".ico", "image/x-icon"},
    {NULL, NULL}
};

// 添加在文件开头的常量定义部分
#define MAX_CONTENT_LENGTH 1048576  // 1MB 最大 POST 数据大小

// 新增获取 MIME 类型的辅助函数
const char* get_mime_type(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot) return "application/octet-stream";
    
    for (const struct mime_type *m = mime_types; m->extension; m++) {
        if (strcasecmp(dot, m->extension) == 0) {
            return m->type;
        }
    }
    return "application/octet-stream";
}

// 新增函数声明
void handle_get_head_request(int client_sock, Request *request);
void handle_post_request(int client_sock, char *buf, ssize_t readret);
void handle_unsupported_method(int client_sock);

int close_socket(int sock)
{
    if (close(sock))
    {
        LOG_ERROR("Failed closing socket: %s", strerror(errno));
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
        LOG_ERROR("Error listening on socket: %s", strerror(errno));
        close_socket(sock);
        return EXIT_FAILURE;
    }

    /* finally, loop waiting for input and then write it back */
    while (1)
    {
        cli_size = sizeof(cli_addr);
        if ((client_sock = accept(sock, (struct sockaddr *)&cli_addr,
                                  &cli_size)) == -1)
        {
            LOG_ERROR("Error accepting connection: %s", strerror(errno));
            close_socket(sock);
            return EXIT_FAILURE;
        }

        LOG_INFO("New client connected from %s:%d",
                 inet_ntoa(cli_addr.sin_addr),
                 ntohs(cli_addr.sin_port));

        readret = 0;
        memset(buf, 0, BUF_SIZE); // 清空缓冲区

        // 在处理请求开始时记录时间
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        while ((readret = recv(client_sock, buf, BUF_SIZE -1, 0)) >= 1)
        {
            buf[readret] = '\0'; // 确保字符串结束

            // 调用 parse() 函数解析请求
            Request *request = parse(buf, readret);
            if (request == NULL)
            {
                LOG_ERROR("Failed to parse request");
                send(client_sock, RESPONSE_400, strlen(RESPONSE_400), 0);
            }
            else if (strcmp(request->http_version, "HTTP/1.1") != 0){
                LOG_ERROR("Unsupported HTTP version: %s", request->http_version);
                send(client_sock, RESPONSE_505, strlen(RESPONSE_505), 0);
            }
            else
            {
                // 检查 request 的各个字段是否为 NULL
                if (request->http_method != NULL && request->http_uri != NULL && request->http_version != NULL)
                {
                    LOG_INFO("Received %s request for %s", request->http_method, request->http_uri);

                    if (strcmp(request->http_method, "GET") == 0 ||
                        strcmp(request->http_method, "HEAD") == 0)
                    {
                        handle_get_head_request(client_sock, request);
                    }
                    else if(strcmp(request->http_method, "POST") == 0)
                    {
                        handle_post_request(client_sock, buf, readret);
                    }
                    else
                    {
                        handle_unsupported_method(client_sock);
                    }
                }
                else
                {
                    LOG_ERROR("Invalid request structure");
                    send(client_sock, RESPONSE_400, strlen(RESPONSE_400), 0);
                }

                // 释放请求结构体
                if (request->headers != NULL)
                {
                    free(request->headers);
                }
                free(request);

                // 判断是否为持久连接
                if (!strstr(buf, "Connection: keep-alive"))
                 {
                     break;
                 }
            }

            memset(buf, 0, BUF_SIZE);
        }

        if (readret == -1)
        {
            LOG_ERROR("Error reading from client socket: %s", strerror(errno));
            close_socket(client_sock);
            close_socket(sock);
            return EXIT_FAILURE;
        }
        else if (readret == 0)
        {
            LOG_INFO("Client closed connection");
        }

        if (close_socket(client_sock))
        {
            LOG_ERROR("Error closing client socket");
            close_socket(sock);
            return EXIT_FAILURE;
        }

        clock_gettime(CLOCK_MONOTONIC, &end);
        double time_spent = (end.tv_sec - start.tv_sec) + 
                           (end.tv_nsec - start.tv_nsec) / 1e9;
        LOG_INFO("Request processed in %.3f seconds", time_spent);
    }

    close_socket(sock);

    log_close();
    return EXIT_SUCCESS;
}

void handle_get_head_request(int client_sock, Request *request)
{
    char file_path[BUF_SIZE] = "static_site";
    
    // 处理根路径请求
    if (strcmp(request->http_uri, "/") == 0) {
        strcat(file_path, "/index.html");
    } else {
        strncat(file_path, request->http_uri, BUF_SIZE - strlen(file_path) - 1);
    }

    FILE *file = fopen(file_path, "rb");  // 使用二进制模式打开文件
    if (!file)
    {
        LOG_ERROR("Cannot open file: %s", file_path);
        send(client_sock, RESPONSE_404, strlen(RESPONSE_404), 0);
        return;
    }

    struct stat path_stat;
    if (stat(file_path, &path_stat) != 0 || !S_ISREG(path_stat.st_mode)) {
        LOG_ERROR("File not found or invalid path: %s", file_path);
        send(client_sock, RESPONSE_404, strlen(RESPONSE_404), 0);
        fclose(file);
        return;
    }

    // 获取文件的 MIME 类型
    const char *content_type = get_mime_type(file_path);
    
    // 构建响应头
    char response_header[BUF_SIZE];
    snprintf(response_header, BUF_SIZE,
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "\r\n",
             content_type,
             path_stat.st_size);
    
    send(client_sock, response_header, strlen(response_header), 0);

    // 如果是 GET 请求，发送文件内容
    if (strcmp(request->http_method, "GET") == 0)
    {
        char file_buf[BUF_SIZE];
        size_t file_read;
        while ((file_read = fread(file_buf, 1, BUF_SIZE, file)) > 0)
        {
            send(client_sock, file_buf, file_read, 0);
        }
    }

    fclose(file);
}

void handle_post_request(int client_sock, char *buf, ssize_t readret)
{
    // 查找 Content-Length 头部
    char *content_length_str = strstr(buf, "Content-Length: ");
    if (!content_length_str) {
        LOG_ERROR("No Content-Length found in POST request");
        send(client_sock, RESPONSE_400, strlen(RESPONSE_400), 0);
        send(client_sock, buf, readret, 0);
        return;
    }

    // 解析 Content-Length
    long content_length = strtol(content_length_str + 16, NULL, 10);
    if (content_length <= 0 || content_length > MAX_CONTENT_LENGTH) {
        LOG_ERROR("Invalid Content-Length: %ld", content_length);
        send(client_sock, RESPONSE_400, strlen(RESPONSE_400), 0);
        send(client_sock, buf, readret, 0);
        return;
    }

    // 查找请求体开始位置（空行之后）
    char *body_start = strstr(buf, "\r\n\r\n");
    if (!body_start) {
        LOG_ERROR("No request body found");
        send(client_sock, RESPONSE_400, strlen(RESPONSE_400), 0);
        send(client_sock, buf, readret, 0);
        return;
    }
    body_start += 4;  // 跳过 \r\n\r\n

    // 计算已经读取的请求体长度
    long body_received = readret - (body_start - buf);
    
    // 如果需要，继续读取剩余的请求体
    char *post_data = malloc(content_length + 1);
    if (!post_data) {
        LOG_ERROR("Failed to allocate memory for POST data");
        send(client_sock, RESPONSE_500, strlen(RESPONSE_500), 0);
        return;
    }

    // 复制已经读取的部分
    memcpy(post_data, body_start, body_received);
    
    // 如果还有数据需要读取
    while (body_received < content_length) {
        ssize_t remaining = content_length - body_received;
        ssize_t to_read = remaining > BUF_SIZE ? BUF_SIZE : remaining;
        
        ssize_t bytes = recv(client_sock, post_data + body_received, to_read, 0);
        if (bytes <= 0) {
            LOG_ERROR("Failed to read complete POST data");
            free(post_data);
            send(client_sock, RESPONSE_400, strlen(RESPONSE_400), 0);
            return;
        }
        body_received += bytes;
    }
    post_data[content_length] = '\0';

    // 记录接收到的 POST 数据
    LOG_INFO("Received POST data: %s", post_data);

    // 处理 POST 数据（这里简单地返回接收到的数据）
    char response[BUF_SIZE];
    snprintf(response, BUF_SIZE,
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/plain\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "\r\n",
             strlen(post_data) + 19, post_data);

    send(client_sock, response, strlen(response), 0);
    send(client_sock, buf, readret, 0);
    
    free(post_data);
}

void handle_unsupported_method(int client_sock)
{
    send(client_sock, RESPONSE_501, strlen(RESPONSE_501), 0);
}
