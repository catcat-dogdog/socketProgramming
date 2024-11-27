#ifndef ECHO_SERVER_H
#define ECHO_SERVER_H

#include "client_handler.h"
#include <netinet/in.h>

#define ECHO_PORT 9999
#define MAX_CLIENTS 1024
#define TIMEOUT_SECS 5

typedef struct {
    int server_sock;
    struct sockaddr_in server_addr;
    client_t clients[MAX_CLIENTS];
    int is_running;
} server_t;

// 服务器相关函数
int server_init(server_t *server);
int server_run(server_t *server);
void server_cleanup(server_t *server);

#endif