#include "request_queue.h"
#include <string.h>

RequestQueue* request_queue_create(void) {
    RequestQueue* queue = (RequestQueue*)malloc(sizeof(RequestQueue));
    if (!queue) {
        return NULL;
    }
    
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    
    return queue;
}

void request_queue_destroy(RequestQueue* queue) {
    if (!queue) {
        return;
    }
    
    // 清理所有节点
    struct RequestNode* current = queue->head;
    while (current) {
        struct RequestNode* next = current->next;
        free(current->data);
        free(current);
        current = next;
    }
    
    free(queue);
}

bool request_queue_push(RequestQueue* queue, const char* data, size_t size) {
    if (!queue || !data || size == 0) {
        return false;
    }
    
    // 创建新节点
    struct RequestNode* node = (struct RequestNode*)malloc(sizeof(struct RequestNode));
    if (!node) {
        return false;
    }
    
    // 分配并复制数据
    node->data = (char*)malloc(size + 1);
    if (!node->data) {
        free(node);
        return false;
    }
    
    memcpy(node->data, data, size);
    node->data[size] = '\0';
    node->size = size;
    node->next = NULL;
    
    // 添加到队列
    if (queue->tail) {
        queue->tail->next = node;
        queue->tail = node;
    } else {
        queue->head = queue->tail = node;
    }
    
    queue->count++;
    return true;
}

char* request_queue_pop(RequestQueue* queue, size_t* size) {
    if (!queue || !queue->head) {
        if (size) {
            *size = 0;
        }
        return NULL;
    }
    
    // 获取头节点
    struct RequestNode* node = queue->head;
    char* data = node->data;
    if (size) {
        *size = node->size;
    }
    
    // 更新队列
    queue->head = node->next;
    if (!queue->head) {
        queue->tail = NULL;
    }
    queue->count--;
    
    // 释放节点（但保留数据）
    free(node);
    
    return data;
}

int request_queue_size(const RequestQueue* queue) {
    return queue ? queue->count : 0;
}