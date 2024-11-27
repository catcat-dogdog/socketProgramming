#include "client_handler.h"
#include "http_response.h"
#include "logger.h"
#include "parse.h"
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

#define PATH_MAX 1024
#define DEFAULT_PATH "static_site"

void client_init(client_t *client, int sockfd, struct sockaddr_in addr, size_t buffer_size)
{
    client->sockfd = sockfd;
    client->addr = addr;
    client->buffer = malloc(buffer_size);
    client->buf_size = buffer_size;
    client->buf_len = 0;
    client->last_active = time(NULL);
    client->queue = request_queue_create();
}

void client_destroy(client_t *client)
{
    if (client->sockfd > 0)
    {
        close(client->sockfd);
    }
    free(client->buffer);
    request_queue_destroy(client->queue);

    // 重置结构体
    memset(client, 0, sizeof(client_t));
}

void client_handle(client_t *client)
{
    ssize_t bytes_read = recv(client->sockfd,
                              client->buffer + client->buf_len,
                              client->buf_size - client->buf_len - 1,
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
        if (!request_queue_push(client->queue, current_pos, request_size))
        {
            LOG_ERROR("Failed to enqueue request");
            http_send_status(client->sockfd, HTTP_STATUS_INTERNAL_ERROR);
            return;
        }

        current_pos = request_end + 4;

        // 处理队列中的请求
        while (request_queue_size(client->queue) > 0 &&
               request_queue_size(client->queue) <= MAX_REQUESTS_IN_PIPELINE)
        {
            size_t request_len;
            char *request_data = request_queue_pop(client->queue, &request_len);
            if (!request_data)
            {
                break;
            }

            // 解析并处理请求
            Request *request = parse(request_data, request_len);
            if (request)
            {
                if (strcmp(request->http_method, "GET") == 0)
                {
                    char full_path[PATH_MAX] = DEFAULT_PATH;
                    if (strcmp(request->http_uri, "/") == 0)
                    {
                        strcat(full_path, "/index.html");
                    }
                    else
                    {
                        strcat(full_path, request->http_uri);
                    }
                    http_get_response(client->sockfd, full_path);
                }
                else if (strcmp(request->http_method, "HEAD") == 0)
                {
                    char full_path[PATH_MAX] = DEFAULT_PATH;
                    if (strcmp(request->http_uri, "/") == 0)
                    {
                        strcat(full_path, "/index.html");
                    }
                    else
                    {
                        strcat(full_path, request->http_uri);
                    }
                    http_head_response(client->sockfd, full_path);
                }
                else if (strcmp(request->http_method, "POST") == 0)
                {
                    http_post_response(client->sockfd, request_data, request_len);
                }
                else
                {
                    http_send_status(client->sockfd, HTTP_STATUS_NOT_IMPLEMENTED);
                }
                free(request);
            }
            else
            {
                http_send_status(client->sockfd, HTTP_STATUS_BAD_REQUEST);
            }

            free(request_data);
        }

        // 检查队列大小限制
        if (request_queue_size(client->queue) > MAX_REQUESTS_IN_PIPELINE)
        {
            LOG_ERROR("Too many requests in pipeline");
            http_send_status(client->sockfd, HTTP_STATUS_INTERNAL_ERROR);
            return;
        }
    }

    // 处理剩余数据
    size_t remaining = client->buf_len - (current_pos - client->buffer);
    if (remaining > 0 && current_pos != client->buffer)
    {
        memmove(client->buffer, current_pos, remaining);
        client->buf_len = remaining;
    }
    else if (current_pos == client->buffer)
    {
        if (client->buf_len >= client->buf_size - 1)
        {
            LOG_ERROR("Request too large");
            http_send_status(client->sockfd, HTTP_STATUS_BAD_REQUEST);
            client->buf_len = 0;
        }
    }
    else
    {
        client->buf_len = 0;
    }
}

bool client_is_timeout(const client_t *client, time_t timeout_secs)
{
    return (time(NULL) - client->last_active) > timeout_secs;
}