/******************************************************************************
 * echo_client.c                                                               *
 *                                                                             *
 * Description: This file contains the C source code for an echo client.  The  *
 *              client connects to an arbitrary <host,port> and sends input    *
 *              from stdin.                                                    *
 *                                                                             *
 * Authors: Athula Balachandran <abalacha@cs.cmu.edu>,                         *
 *          Wolf Richter <wolf@cs.cmu.edu>                                     *
 *                                                                             *
 *******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <assert.h>

#define ECHO_PORT 9999
#define BUF_SIZE 4096
#define MAX_CONCURRENT_REQUESTS 5
#define REQUEST_TEMPLATES 3

// 请求结构体
typedef struct {
    int sock;
    const char *request;
    int request_id;
} RequestArgs;

// 预定义的HTTP请求模板
const char *request_templates[] = {
    "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n",
    "GET /style.css HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n",
    "GET /script.js HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n"
};

// 线程函数
void* send_request(void *arg) {
    RequestArgs *args = (RequestArgs *)arg;
    char buf[BUF_SIZE];
    
    fprintf(stdout, "[Thread %d] Sending request: %s\n", args->request_id, args->request);
    
    if (send(args->sock, args->request, strlen(args->request), 0) < 0) {
        fprintf(stderr, "[Thread %d] Failed to send request\n", args->request_id);
        return NULL;
    }

    int bytes_received;
    if ((bytes_received = recv(args->sock, buf, BUF_SIZE, 0)) > 0) {
        buf[bytes_received] = '\0';
        fprintf(stdout, "[Thread %d] Received response (%d bytes):\n%s\n", 
                args->request_id, bytes_received, buf);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <server-ip> <port>", argv[0]);
        return EXIT_FAILURE;
    }

    // 设置连接参数
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *servinfo;
    int status;
    if ((status = getaddrinfo(argv[1], argv[2], &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return EXIT_FAILURE;
    }

    // 创建socket
    int sock;
    if ((sock = socket(servinfo->ai_family, servinfo->ai_socktype, 
                      servinfo->ai_protocol)) == -1) {
        fprintf(stderr, "Socket failed\n");
        return EXIT_FAILURE;
    }

    // 连接服务器
    if (connect(sock, servinfo->ai_addr, servinfo->ai_addrlen) == -1) {
        fprintf(stderr, "Connect failed\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "Connected to server, starting pipeline requests...\n");

    // 创建线程数组
    pthread_t threads[MAX_CONCURRENT_REQUESTS];
    RequestArgs args[MAX_CONCURRENT_REQUESTS];

    // 发起多个并发请求
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++) {
        args[i].sock = sock;
        args[i].request = request_templates[i % REQUEST_TEMPLATES];
        args[i].request_id = i + 1;

        if (pthread_create(&threads[i], NULL, send_request, &args[i]) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            continue;
        }
        
        // 短暂延迟，避免请求完全同时发送
        usleep(10000); // 10ms delay
    }

    // 等待所有线程完成
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++) {
        pthread_join(threads[i], NULL);
    }

    fprintf(stdout, "All requests completed\n");

    // 清理资源
    freeaddrinfo(servinfo);
    close(sock);
    return EXIT_SUCCESS;
}
