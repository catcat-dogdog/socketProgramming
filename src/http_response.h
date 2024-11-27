#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <stdlib.h>
#include <stdbool.h>

// HTTP 响应状态码
#define HTTP_STATUS_OK                200
#define HTTP_STATUS_BAD_REQUEST       400
#define HTTP_STATUS_NOT_FOUND         404
#define HTTP_STATUS_INTERNAL_ERROR    500
#define HTTP_STATUS_NOT_IMPLEMENTED   501
#define HTTP_STATUS_VERSION_NOT_SUPPORTED 505

// MIME 类型结构体
struct mime_type {
    const char *extension;
    const char *type;
};

// 响应处理函数
void http_send_status(int client_sock, int status_code);
void http_get_response(int client_sock, const char* filepath);
void http_head_response(int client_sock, const char* filepath);
void http_post_response(int client_sock, const char* data, size_t length);
const char* http_get_mime_type(const char* filename);

#endif