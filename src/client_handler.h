#ifndef CLIENT_HANDLER_H
#define CLIENT_HANDLER_H

#include "request_queue.h"
#include <netinet/in.h>
#include <time.h>
#include <stdbool.h>

#define BUF_SIZE 4096
#define MAX_REQUESTS_IN_PIPELINE 10
#define MAX_CONTENT_LENGTH 1048576 // 1MB 最大 POST 数据大小

// 客户端上下文结构体
typedef struct {
    int sockfd;                    // 客户端socket
    struct sockaddr_in addr;       // 客户端地址
    char* buffer;                  // 接收缓冲区
    size_t buf_size;              // 缓冲区大小
    size_t buf_len;               // 当前缓冲区使用长度
    time_t last_active;           // 最后活动时间
    RequestQueue* queue;          // 请求队列
} client_t;

// 函数声明
void client_init(client_t* client, int sockfd, struct sockaddr_in addr, size_t buffer_size);
void client_destroy(client_t* client);
void client_handle(client_t* client);
bool client_is_timeout(const client_t* client, time_t timeout_secs);

#endif