#ifndef REQUEST_QUEUE_H
#define REQUEST_QUEUE_H

#include <stdlib.h>
#include <stdbool.h>

// 请求队列节点结构体
struct RequestNode {
    char *data;
    size_t size;
    struct RequestNode *next;
};

// 请求队列结构体
typedef struct RequestQueue {
    struct RequestNode *head;
    struct RequestNode *tail;
    int count;
} RequestQueue;

RequestQueue* request_queue_create(void);
void request_queue_destroy(RequestQueue* queue);
bool request_queue_push(RequestQueue* queue, const char* data, size_t size);
char* request_queue_pop(RequestQueue* queue, size_t* size);
int request_queue_size(const RequestQueue* queue);

#endif