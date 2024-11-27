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
#include <stdbool.h>

#define ECHO_PORT 9999
#define BUF_SIZE 4096
#define MAX_REQUESTS_IN_PIPELINE 10
#define MAX_CLIENTS 1024           // 最大客户端连接数
#define TIMEOUT_SECS 5             // select超时时间(秒)
#define MAX_CONTENT_LENGTH 1048576 // 1MB 最大 POST 数据大小

// 响应状态码定义
#define RESPONSE_200 "HTTP/1.1 200 OK\r\n"
#define RESPONSE_400 "HTTP/1.1 400 Bad Request\r\n\r\n"
#define RESPONSE_404 "HTTP/1.1 404 Not Found\r\n\r\n"
#define RESPONSE_500 "HTTP/1.1 500 Internal Server Error\r\n\r\n"
#define RESPONSE_501 "HTTP/1.1 501 Not Implemented\r\n\r\n"
#define RESPONSE_505 "HTTP/1.1 505 HTTP Version Not Supported\r\n\r\n"

int close_socket(int sock)
{
    if (close(sock))
    {
        LOG_ERROR("Failed closing socket: %s", strerror(errno));
        return 1;
    }
    return 0;
}

// 修改 RequestQueue 结构体
struct RequestQueue
{
    struct RequestNode
    {
        char *data;
        size_t size;
        struct RequestNode *next;
    } *head, *tail;
    int count;
};

// 修改客户端结构体，添加请求队列
typedef struct
{
    int sockfd;
    struct sockaddr_in addr;
    char buffer[BUF_SIZE];
    size_t buf_len;
    time_t last_active;
    struct RequestQueue queue; // 添加请求队列
} client_t;

// 初始化请求队列
void initQueue(struct RequestQueue *queue)
{
    queue->head = queue->tail = NULL;
    queue->count = 0;
}

// 添加请求到队列
bool enqueueRequest(struct RequestQueue *queue, const char *data, size_t size)
{
    struct RequestNode *node = malloc(sizeof(struct RequestNode));
    if (!node)
        return false;

    node->data = malloc(size + 1);
    if (!node->data)
    {
        free(node);
        return false;
    }

    memcpy(node->data, data, size);
    node->data[size] = '\0';
    node->size = size;
    node->next = NULL;

    if (queue->tail)
    {
        queue->tail->next = node;
    }
    else
    {
        queue->head = node;
    }
    queue->tail = node;
    queue->count++;

    return true;
}

// 从队列中获取请求
struct RequestNode *dequeueRequest(struct RequestQueue *queue)
{
    if (!queue->head)
        return NULL;

    struct RequestNode *node = queue->head;
    queue->head = node->next;
    if (!queue->head)
    {
        queue->tail = NULL;
    }
    queue->count--;
    return node;
}

// 客户端请求处理
void handle_client(client_t *client);
void handle_get_head_request(int client_sock, Request *request);
void handle_post_request(int client_sock, char *buf, ssize_t readret);
void handle_unsupported_method(int client_sock);

int main(int argc, char *argv[])
{
    int server_sock;
    struct sockaddr_in server_addr;
    client_t clients[MAX_CLIENTS] = {0}; // 初始化客户端数组

    fprintf(stdout, "----- Echo Server -----\n");

    // 初始化日志
    if (log_init("server.log") != 0)
    {
        fprintf(stderr, "Failed to initialize logger.\n");
        return EXIT_FAILURE;
    }

    LOG_INFO("Echo Server starting...");

    // 创建一个socket
    if ((server_sock = socket(PF_INET, SOCK_STREAM, 0)) == -1)
    {
        LOG_ERROR("Failed creating socket");
        log_close();
        return EXIT_FAILURE;
    }

    // 添加 socket 重用选项
    int optval = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
    {
        LOG_ERROR("Failed to set socket options");
        close_socket(server_sock);
        log_close();
        return EXIT_FAILURE;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ECHO_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    /* servers bind sockets to ports---notify the OS they accept connections */
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)))
    {
        LOG_ERROR("Failed binding socket: %s", strerror(errno));
        close_socket(server_sock); // 确保在错误时关闭 socket
        log_close();
        return EXIT_FAILURE;
    }

    if (listen(server_sock, 5))
    {
        LOG_ERROR("Error listening on socket: %s", strerror(errno));
        close_socket(server_sock);
        log_close();
        return EXIT_FAILURE;
    }

    // 设置非阻塞
    int flags = fcntl(server_sock, F_GETFL, 0);
    fcntl(server_sock, F_SETFL, flags | O_NONBLOCK);

    while (1)
    {
        fd_set read_fds;
        struct timeval tv;
        int max_fd = server_sock;

        FD_ZERO(&read_fds);
        FD_SET(server_sock, &read_fds);

        // 添加所有活跃的客户端到fd_set
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].sockfd > 0)
            {
                FD_SET(clients[i].sockfd, &read_fds);
                if (clients[i].sockfd > max_fd)
                {
                    max_fd = clients[i].sockfd;
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
        if (FD_ISSET(server_sock, &read_fds))
        {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);

            if (client_sock < 0)
            {
                LOG_ERROR("Accept failed: %s", strerror(errno));
                continue;
            }

            // 设置新socket为非阻塞
            flags = fcntl(client_sock, F_GETFL, 0);
            fcntl(client_sock, F_SETFL, flags | O_NONBLOCK);

            // 获取客户端IP地址
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

            // 查找空闲的客户端槽位
            int i;
            for (i = 0; i < MAX_CLIENTS; i++)
            {
                if (clients[i].sockfd == 0)
                {
                    clients[i].sockfd = client_sock;
                    clients[i].addr = client_addr;
                    clients[i].buf_len = 0;
                    clients[i].last_active = time(NULL);
                    initQueue(&clients[i].queue); // 初始化请求队列

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
            if (clients[i].sockfd > 0 && FD_ISSET(clients[i].sockfd, &read_fds))
            {
                handle_client(&clients[i]);
            }
        }

        // 检查超时连接
        time_t current_time = time(NULL);
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].sockfd > 0 &&
                (current_time - clients[i].last_active) > TIMEOUT_SECS)
            {
                close(clients[i].sockfd);
                clients[i].sockfd = 0;
            }
        }
    }

    // 清理资源
    close(server_sock);
    log_close();
    return EXIT_SUCCESS;
}

struct mime_type
{
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
    {NULL, NULL}};

// 新增获取 MIME 类型的辅助函数
const char *get_mime_type(const char *filename)
{
    const char *dot = strrchr(filename, '.');
    if (!dot)
        return "application/octet-stream";

    for (const struct mime_type *m = mime_types; m->extension; m++)
    {
        if (strcasecmp(dot, m->extension) == 0)
        {
            return m->type;
        }
    }
    return "application/octet-stream";
}

void handle_get_head_request(int client_sock, Request *request)
{
    LOG_INFO("Processing %s request for URI: %s", request->http_method, request->http_uri);

    char file_path[BUF_SIZE] = "static_site";

    // 处理根路径请求
    if (strcmp(request->http_uri, "/") == 0)
    {
        strcat(file_path, "/index.html");
    }
    else
    {
        strncat(file_path, request->http_uri, BUF_SIZE - strlen(file_path) - 1);
    }

    FILE *file = fopen(file_path, "rb"); // 使用二进制模式打开文件
    if (!file)
    {
        LOG_ERROR("Cannot open file: %s", file_path);
        send(client_sock, RESPONSE_404, strlen(RESPONSE_404), 0);
        LOG_INFO("Sent 404 response");
        return;
    }

    struct stat path_stat;
    if (stat(file_path, &path_stat) != 0 || !S_ISREG(path_stat.st_mode))
    {
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
    // 如果是 HEAD 请求，只发送响应头
    if (strcmp(request->http_method, "HEAD") == 0)
    {
        send(client_sock, response_header, strlen(response_header), 0);
        LOG_INFO("Sent HEAD response headers for: %s", file_path);
        return;
    }

    // 如果是 GET 请求，发送文件内容
    if (strcmp(request->http_method, "GET") == 0)
    {
        LOG_INFO("Starting to send file content for: %s", file_path);
        char file_buf[BUF_SIZE];
        size_t file_read;
        size_t total_sent = 0;

        file_read = fread(file_buf, 1, BUF_SIZE - strlen(response_header) - 1, file);
        strcat(response_header, file_buf);
        send(client_sock, response_header, strlen(response_header), 0);
        total_sent += file_read;

        while ((file_read = fread(file_buf, 1, BUF_SIZE, file)) > 0)
        {
            send(client_sock, file_buf, file_read, 0);
            total_sent += file_read;
        }
        LOG_INFO("Completed sending file: %s, total bytes sent: %zu", file_path, total_sent);
    }

    fclose(file);
}

void handle_post_request(int client_sock, char *buf, ssize_t readret)
{
    LOG_INFO("Processing POST request");

    char *content_length_str = strstr(buf, "Content-Length: ");
    if (!content_length_str)
    {
        LOG_ERROR("POST request missing Content-Length header");
        send(client_sock, RESPONSE_400, strlen(RESPONSE_400), 0);
        send(client_sock, buf, readret, 0);
        return;
    }

    // 解析 Content-Length
    long content_length = strtol(content_length_str + 16, NULL, 10);
    LOG_INFO("POST request Content-Length: %ld bytes", content_length);

    if (content_length <= 0 || content_length > MAX_CONTENT_LENGTH)
    {
        LOG_ERROR("Invalid Content-Length: %ld (max allowed: %d)", content_length, MAX_CONTENT_LENGTH);
        send(client_sock, RESPONSE_400, strlen(RESPONSE_400), 0);
        send(client_sock, buf, readret, 0);
        return;
    }

    // 查找请求体开始位置（空行之后）
    char *body_start = strstr(buf, "\r\n\r\n");
    if (!body_start)
    {
        LOG_ERROR("No request body found");
        send(client_sock, RESPONSE_400, strlen(RESPONSE_400), 0);
        send(client_sock, buf, readret, 0);
        return;
    }
    body_start += 4; // 跳过 \r\n\r\n

    // 计算已经读取的请求体长度
    long body_received = readret - (body_start - buf);

    // 如果需要，继续读取剩余的请求体
    char *post_data = malloc(content_length + 1);
    if (!post_data)
    {
        LOG_ERROR("Failed to allocate memory for POST data");
        send(client_sock, RESPONSE_500, strlen(RESPONSE_500), 0);
        return;
    }

    // 复制已经读取的部分
    memcpy(post_data, body_start, body_received);

    // 如果还有数据需要读取
    while (body_received < content_length)
    {
        ssize_t remaining = content_length - body_received;
        ssize_t to_read = remaining > BUF_SIZE ? BUF_SIZE : remaining;

        ssize_t bytes = recv(client_sock, post_data + body_received, to_read, 0);
        if (bytes <= 0)
        {
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
             strlen(post_data) + 19);

    send(client_sock, response, strlen(response), 0);
    send(client_sock, buf, readret, 0);

    LOG_INFO("Successfully received all POST data");
    free(post_data);
}

void handle_unsupported_method(int client_sock)
{
    LOG_WARN("Received request with unsupported HTTP method");
    send(client_sock, RESPONSE_501, strlen(RESPONSE_501), 0);
    LOG_INFO("Sent 501 Not Implemented response");
}

// 处理客户端
void handle_client(client_t *client)
{
    ssize_t bytes_read = recv(client->sockfd,
                              client->buffer + client->buf_len,
                              BUF_SIZE - client->buf_len - 1,
                              0);

    if (bytes_read <= 0)
    {
        // 连接关闭或错误
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client->addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        LOG_INFO("Client %s:%d disconnected", client_ip, ntohs(client->addr.sin_port));
        close(client->sockfd);
        client->sockfd = 0;
        return;
    }

    client->buf_len += bytes_read;
    client->buffer[client->buf_len] = '\0';
    client->last_active = time(NULL);

    // 处理pipeline请求
    char *current_pos = client->buffer;
    char *request_end;

    while ((request_end = strstr(current_pos, "\r\n\r\n")))
    {
        size_t request_size = request_end - current_pos + 4;

        // 将完整的请求加入队列
        if (!enqueueRequest(&client->queue, current_pos, request_size))
        {
            LOG_ERROR("Failed to enqueue request");
            send(client->sockfd, RESPONSE_500, strlen(RESPONSE_500), 0);
            return;
        }

        current_pos = request_end + 4;

        // 处理队列中的请求（最多处理MAX_REQUESTS_IN_PIPELINE个）
        while (client->queue.count > 0 &&
               client->queue.count <= MAX_REQUESTS_IN_PIPELINE)
        {
            struct RequestNode *node = dequeueRequest(&client->queue);
            if (!node)
                break;

            // 解析并处理请求
            Request *request = parse(node->data, node->size);
            if (request)
            {
                if (strcmp(request->http_method, "GET") == 0 ||
                    strcmp(request->http_method, "HEAD") == 0)
                {
                    handle_get_head_request(client->sockfd, request);
                }
                else if (strcmp(request->http_method, "POST") == 0)
                {
                    handle_post_request(client->sockfd, node->data, node->size);
                }
                else
                {
                    handle_unsupported_method(client->sockfd);
                }
                free(request);
            }
            else
            {
                send(client->sockfd, RESPONSE_400, strlen(RESPONSE_400), 0);
            }

            // 清理节点
            free(node->data);
            free(node);
        }

        // 如果队列中的请求数超过限制，返回错误
        if (client->queue.count > MAX_REQUESTS_IN_PIPELINE)
        {
            LOG_ERROR("Too many requests in pipeline");
            send(client->sockfd, RESPONSE_500, strlen(RESPONSE_500), 0);
            return;
        }
    }

    // 移动未处理的数据到缓冲区开始
    size_t remaining = client->buf_len - (current_pos - client->buffer);
    if (remaining > 0 && current_pos != client->buffer)
    {
        memmove(client->buffer, current_pos, remaining);
        client->buf_len = remaining;
    }
    else if (current_pos == client->buffer)
    {
        // 如果没有找到完整请求，检查缓冲区是否已满
        if (client->buf_len >= BUF_SIZE - 1)
        {
            LOG_ERROR("Request too large");
            send(client->sockfd, RESPONSE_400, strlen(RESPONSE_400), 0);
            client->buf_len = 0;
        }
    }
    else
    {
        client->buf_len = 0;
    }
}
