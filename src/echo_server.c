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
#include <sys/epoll.h>
#include <pthread.h>
#include <stdbool.h>

#define ECHO_PORT 9999
#define BUF_SIZE 4096
#define MAX_REQUESTS_IN_PIPELINE 10
#define MAX_EVENTS 1024
#define MAX_THREADS 8
#define THREAD_QUEUE_SIZE 1024

// 响应状态码定义
#define RESPONSE_200 "HTTP/1.1 200 OK\r\n"
#define RESPONSE_400 "HTTP/1.1 400 Bad Request\r\n\r\n"
#define RESPONSE_404 "HTTP/1.1 404 Not Found\r\n\r\n"
#define RESPONSE_500 "HTTP/1.1 500 Internal Server Error\r\n\r\n"
#define RESPONSE_501 "HTTP/1.1 501 Not Implemented\r\n\r\n"
#define RESPONSE_505 "HTTP/1.1 505 HTTP Version Not Supported\r\n\r\n"

// 添加在文件开头的全局变量部分
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

// 添加在文件开头的常量定义部分
#define MAX_CONTENT_LENGTH 1048576 // 1MB 最大 POST 数据大小

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
    pthread_mutex_t mutex;
};

// 初始化请求队列
void initQueue(struct RequestQueue *queue)
{
    queue->head = queue->tail = NULL;
    queue->count = 0;
    pthread_mutex_init(&queue->mutex, NULL);
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

    pthread_mutex_lock(&queue->mutex);
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
    pthread_mutex_unlock(&queue->mutex);

    return true;
}

// 从队列中获取请求
struct RequestNode *dequeueRequest(struct RequestQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);
    if (!queue->head)
    {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }

    struct RequestNode *node = queue->head;
    queue->head = node->next;
    if (!queue->head)
    {
        queue->tail = NULL;
    }
    queue->count--;
    pthread_mutex_unlock(&queue->mutex);

    return node;
}

// 线程池结构
typedef struct
{
    pthread_t *threads;
    int thread_count;

    // 任务队列
    struct
    {
        int client_sock;
        struct sockaddr_in client_addr;
    } *tasks;

    int task_capacity;
    int task_size;
    int task_front;
    int task_rear;

    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_not_empty;
    pthread_cond_t queue_not_full;
    bool shutdown;
} ThreadPool;

// 声明线程池函数
ThreadPool *thread_pool_create(int thread_count);
void thread_pool_destroy(ThreadPool *pool);
void thread_pool_add_task(ThreadPool *pool, int client_sock, struct sockaddr_in client_addr);
void *worker_thread(void *arg);

int main(int argc, char *argv[])
{
    int sock;
    struct sockaddr_in addr;

    fprintf(stdout, "----- Echo Server -----\n");

    // 初始化日志
    if (log_init("server.log") != 0)
    {
        fprintf(stderr, "Failed to initialize logger.\n");
        return EXIT_FAILURE;
    }

    LOG_INFO("Echo Server starting...");

    // 创建一个socket
    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1)
    {
        LOG_ERROR("Failed creating socket");
        log_close();
        return EXIT_FAILURE;
    }

    // 添加 socket 重用选项
    int optval = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
    {
        LOG_ERROR("Failed to set socket options");
        close_socket(sock);
        log_close();
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
        log_close();
        return EXIT_FAILURE;
    }

    if (listen(sock, 5))
    {
        LOG_ERROR("Error listening on socket: %s", strerror(errno));
        close_socket(sock);
        log_close();
        return EXIT_FAILURE;
    }

    // 创建 epoll 实例
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
    {
        LOG_ERROR("Failed to create epoll instance");
        close_socket(sock);
        log_close();
        return EXIT_FAILURE;
    }

    // 创建线程池
    ThreadPool *thread_pool = thread_pool_create(MAX_THREADS);
    if (!thread_pool)
    {
        LOG_ERROR("Failed to create thread pool");
        close_socket(sock);
        log_close();
        return EXIT_FAILURE;
    }

    // 添加监听 socket 到 epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock, &ev) == -1)
    {
        LOG_ERROR("Failed to add listening socket to epoll");
        close_socket(sock);
        log_close();
        return EXIT_FAILURE;
    }

    // 事件循环
    struct epoll_event events[MAX_EVENTS];
    while (1)
    {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1)
        {
            LOG_ERROR("epoll_wait error");
            continue;
        }

        for (int i = 0; i < nfds; i++)
        {
            if (events[i].data.fd == sock)
            {
                // 处理新连接
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_sock = accept(sock, (struct sockaddr *)&client_addr, &client_len);

                if (client_sock == -1)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        // 非阻塞模式下没有连接可接受
                        continue;
                    }
                    LOG_ERROR("Accept failed: %s", strerror(errno));
                    continue;  // 继续循环而不是退出
                }

                // 设置客户端socket为非阻塞模式
                int flags = fcntl(client_sock, F_GETFL, 0);
                if (flags != -1)
                {
                    fcntl(client_sock, F_SETFL, flags | O_NONBLOCK);
                }

                // 将新客户端添加到线程池
                thread_pool_add_task(thread_pool, client_sock, client_addr);
            }
        }
    }

    // 清理资源
    thread_pool_destroy(thread_pool);
    close(epoll_fd);
    close_socket(sock);
    log_close();
    return EXIT_SUCCESS;
}

// 添加语法解析锁
static pthread_mutex_t parser_mutex = PTHREAD_MUTEX_INITIALIZER;

// 处理客户端请求
void handle_client(int client_sock, struct sockaddr_in client_addr)
{
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    LOG_INFO("New client connection from %s:%d", client_ip, ntohs(client_addr.sin_port));

    char buf[BUF_SIZE];
    struct RequestQueue queue;
    initQueue(&queue);
    size_t partial_len = 0;
    char *partial_buf = NULL;

    // 设置超时时间
    struct timeval tv;
    tv.tv_sec = 5;  // 5秒超时
    tv.tv_usec = 0;
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    while (1) {
        ssize_t readret = recv(client_sock, buf, BUF_SIZE - 1, 0);
        if (readret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 使用select来等待数据
                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(client_sock, &readfds);
                
                struct timeval timeout;
                timeout.tv_sec = 1;  // 1秒超时
                timeout.tv_usec = 0;
                
                int select_result = select(client_sock + 1, &readfds, NULL, NULL, &timeout);
                if (select_result > 0) {
                    continue;  // 有数据可读，继续循环
                } else if (select_result == 0) {
                    // 超时，但继续等待
                    continue;
                } else {
                    // select错误
                    LOG_ERROR("Select error for client %s:%d: %s",
                             client_ip, ntohs(client_addr.sin_port), strerror(errno));
                    break;
                }
            }
            
            LOG_ERROR("Failed to read from client %s:%d: %s",
                      client_ip, ntohs(client_addr.sin_port), strerror(errno));
            break;
        }
        if (readret == 0)
        {
            LOG_INFO("Client %s:%d disconnected", client_ip, ntohs(client_addr.sin_port));
            break;
        }

        buf[readret] = '\0';
        // LOG_DEBUG("Received %zd bytes from %s:%d", readret, client_ip, ntohs(client_addr.sin_port));
        char *current = buf;

        // 处理之前的不完整请求
        if (partial_buf)
        {
            char *new_buf = realloc(partial_buf, partial_len + readret + 1);
            if (!new_buf)
            {
                free(partial_buf);
                break;
            }
            partial_buf = new_buf;
            memcpy(partial_buf + partial_len, buf, readret);
            partial_len += readret;
            partial_buf[partial_len] = '\0';
            current = partial_buf;
        }

        // 解析请求
        char *request_end;
        while ((request_end = strstr(current, "\r\n\r\n")))
        {
            size_t request_len = request_end - current + 4;

            // 将完整请求加入队列
            if (!enqueueRequest(&queue, current, request_len))
            {
                LOG_ERROR("Failed to enqueue request from %s:%d: out of memory",
                          client_ip, ntohs(client_addr.sin_port));
                break;
            }

            // LOG_DEBUG("Enqueued request from %s:%d, queue size: %d",
            //           client_ip, ntohs(client_addr.sin_port), queue.count);

            current = request_end + 4;

            // 处理队列中的请求
            struct RequestNode *node;
            while ((node = dequeueRequest(&queue)))
            {
                // 在解析前加锁
                pthread_mutex_lock(&parser_mutex);
                Request *request = parse(node->data, node->size);
                pthread_mutex_unlock(&parser_mutex);
                
                if (!request) {
                    LOG_ERROR("Parse failed");
                    LOG_INFO("Sending 400 Bad Request response");
                    send(client_sock, RESPONSE_400, strlen(RESPONSE_400), 0);
                    free(node->data);
                    free(node);
                    continue;
                }
                
                // 处理请求
                if (strcmp(request->http_method, "GET") == 0 ||
                    strcmp(request->http_method, "HEAD") == 0)
                {
                    handle_get_head_request(client_sock, request);
                }
                else if (strcmp(request->http_method, "POST") == 0)
                {
                    handle_post_request(client_sock, node->data, node->size);
                }
                else
                {
                    handle_unsupported_method(client_sock);
                }
                free(request);
            }
        }

        // 处理剩余的不完整请求数据
        size_t remaining = (buf + readret) - current;
        if (remaining > 0)
        {
            char *new_partial = malloc(remaining + 1);
            if (!new_partial)
            {
                LOG_ERROR("Failed to allocate memory for partial request from %s:%d",
                          client_ip, ntohs(client_addr.sin_port));
                break;
            }
            memcpy(new_partial, current, remaining);
            new_partial[remaining] = '\0';
            free(partial_buf);
            partial_buf = new_partial;
            partial_len = remaining;
            // LOG_DEBUG("Stored %zu bytes of partial request from %s:%d",
            //           remaining, client_ip, ntohs(client_addr.sin_port));
        }
        else
        {
            free(partial_buf);
            partial_buf = NULL;
            partial_len = 0;
        }
    }

    // 清理资源
    free(partial_buf);
    while (dequeueRequest(&queue))
    {
    } // 清空队列
    pthread_mutex_destroy(&queue.mutex);
    close(client_sock);
    LOG_INFO("Closed connection from %s:%d", client_ip, ntohs(client_addr.sin_port));
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

ThreadPool *thread_pool_create(int thread_count)
{
    ThreadPool *pool = (ThreadPool *)malloc(sizeof(ThreadPool));
    if (!pool)
        return NULL;

    pool->thread_count = thread_count;
    pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * thread_count);
    pool->tasks = malloc(sizeof(*pool->tasks) * THREAD_QUEUE_SIZE);
    pool->task_capacity = THREAD_QUEUE_SIZE;
    pool->task_size = 0;
    pool->task_front = 0;
    pool->task_rear = 0;
    pool->shutdown = false;

    pthread_mutex_init(&pool->queue_mutex, NULL);
    pthread_cond_init(&pool->queue_not_empty, NULL);
    pthread_cond_init(&pool->queue_not_full, NULL);

    // 创建工作线程
    for (int i = 0; i < thread_count; i++)
    {
        pthread_create(&pool->threads[i], NULL, worker_thread, pool);
    }

    return pool;
}

void *worker_thread(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;
    pthread_t tid = pthread_self();
    // LOG_INFO("Worker thread %lu started", (unsigned long)tid);

    while (1)
    {
        pthread_mutex_lock(&pool->queue_mutex);

        while (pool->task_size == 0 && !pool->shutdown)
        {
            // LOG_DEBUG("Thread %lu waiting for task", (unsigned long)tid);
            pthread_cond_wait(&pool->queue_not_empty, &pool->queue_mutex);
        }

        if (pool->shutdown)
        {
            // LOG_INFO("Thread %lu shutting down", (unsigned long)tid);
            pthread_mutex_unlock(&pool->queue_mutex);
            pthread_exit(NULL);
        }

        // 获取任务
        int client_sock = pool->tasks[pool->task_front].client_sock;
        struct sockaddr_in client_addr = pool->tasks[pool->task_front].client_addr;
        pool->task_front = (pool->task_front + 1) % pool->task_capacity;
        pool->task_size--;

        // LOG_INFO("Thread %lu processing new client connection", (unsigned long)tid);

        pthread_mutex_unlock(&pool->queue_mutex);
        pthread_cond_signal(&pool->queue_not_full);

        // 处理客户端请求
        handle_client(client_sock, client_addr);
    }

    return NULL;
}

void thread_pool_add_task(ThreadPool *pool, int client_sock, struct sockaddr_in client_addr)
{
    pthread_mutex_lock(&pool->queue_mutex);

    // 等待队列有空间
    while (pool->task_size == pool->task_capacity && !pool->shutdown)
    {
        pthread_cond_wait(&pool->queue_not_full, &pool->queue_mutex);
    }

    if (pool->shutdown)
    {
        pthread_mutex_unlock(&pool->queue_mutex);
        return;
    }

    // 添加任务到队列
    pool->tasks[pool->task_rear].client_sock = client_sock;
    pool->tasks[pool->task_rear].client_addr = client_addr;
    pool->task_rear = (pool->task_rear + 1) % pool->task_capacity;
    pool->task_size++;

    pthread_mutex_unlock(&pool->queue_mutex);
    pthread_cond_signal(&pool->queue_not_empty);
}

// 同时也应该实现 thread_pool_destroy 函数
void thread_pool_destroy(ThreadPool *pool)
{
    if (pool == NULL)
        return;

    pthread_mutex_lock(&pool->queue_mutex);
    pool->shutdown = true;
    pthread_mutex_unlock(&pool->queue_mutex);

    // 唤醒所有等待的线程
    pthread_cond_broadcast(&pool->queue_not_empty);
    pthread_cond_broadcast(&pool->queue_not_full);

    // 等待所有线程结束
    for (int i = 0; i < pool->thread_count; i++)
    {
        pthread_join(pool->threads[i], NULL);
    }

    // 释放资源
    free(pool->threads);
    free(pool->tasks);
    pthread_mutex_destroy(&pool->queue_mutex);
    pthread_cond_destroy(&pool->queue_not_empty);
    pthread_cond_destroy(&pool->queue_not_full);
    free(pool);
}