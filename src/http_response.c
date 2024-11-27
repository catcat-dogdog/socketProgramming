#include "http_response.h"
#include "logger.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>

#define BUF_SIZE 4096

// MIME 类型定义
static const struct mime_type mime_types[] = {
    {".html", "text/html"},
    {".htm", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".ico", "image/x-icon"},
    {".xml", "application/xml"},
    {".pdf", "application/pdf"},
    {".txt", "text/plain"},
    {NULL, NULL}
};

// 状态码响应
static const char* get_status_message(int status_code) {
    switch (status_code) {
        case HTTP_STATUS_OK:
            return "HTTP/1.1 200 OK\r\n";
        case HTTP_STATUS_BAD_REQUEST:
            return "HTTP/1.1 400 Bad Request\r\n\r\n";
        case HTTP_STATUS_NOT_FOUND:
            return "HTTP/1.1 404 Not Found\r\n\r\n";
        case HTTP_STATUS_INTERNAL_ERROR:
            return "HTTP/1.1 500 Internal Server Error\r\n\r\n";
        case HTTP_STATUS_NOT_IMPLEMENTED:
            return "HTTP/1.1 501 Not Implemented\r\n\r\n";
        case HTTP_STATUS_VERSION_NOT_SUPPORTED:
            return "HTTP/1.1 505 HTTP Version Not Supported\r\n\r\n";
        default:
            return "HTTP/1.1 500 Internal Server Error\r\n\r\n";
    }
}

// 发送文件
static void http_send_file(int client_sock, const char* filepath, bool head_only) {
    struct stat file_stat;
    if (stat(filepath, &file_stat) != 0) {
        LOG_ERROR("File not found: %s", filepath);
        http_send_status(client_sock, HTTP_STATUS_NOT_FOUND);
        return;
    }

    if (!S_ISREG(file_stat.st_mode)) {
        LOG_ERROR("Not a regular file: %s", filepath);
        http_send_status(client_sock, HTTP_STATUS_NOT_FOUND);
        return;
    }

    FILE* file = fopen(filepath, "rb");
    if (!file) {
        LOG_ERROR("Cannot open file: %s (%s)", filepath, strerror(errno));
        http_send_status(client_sock, HTTP_STATUS_INTERNAL_ERROR);
        return;
    }

    // 构建响应头
    const char* content_type = http_get_mime_type(filepath);
    char header[BUF_SIZE];
    snprintf(header, BUF_SIZE,
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "\r\n",
             content_type,
             file_stat.st_size);

    // 发送响应头
    if (send(client_sock, header, strlen(header), 0) < 0) {
        LOG_ERROR("Failed to send file response header: %s", strerror(errno));
        fclose(file);
        return;
    }

    // 如果是 HEAD 请求，到此结束
    if (head_only) {
        LOG_INFO("Sent HEAD response for: %s", filepath);
        fclose(file);
        return;
    }

    // 发送文件内容
    char buf[BUF_SIZE];
    size_t bytes_read;
    size_t total_sent = 0;

    while ((bytes_read = fread(buf, 1, BUF_SIZE, file)) > 0) {
        ssize_t bytes_sent = send(client_sock, buf, bytes_read, 0);
        if (bytes_sent < 0) {
            LOG_ERROR("Failed to send file content: %s", strerror(errno));
            break;
        }
        total_sent += bytes_sent;
    }

    if (ferror(file)) {
        LOG_ERROR("Error reading file: %s", filepath);
    } else {
        LOG_INFO("Sent file: %s, total bytes: %zu", filepath, total_sent);
    }

    fclose(file);
}


void http_send_status(int client_sock, int status_code) {
    const char* response = get_status_message(status_code);
    send(client_sock, response, strlen(response), 0);
    LOG_INFO("Sent status %d response", status_code);
}

void http_get_response(int client_sock, const char* filepath) {
    http_send_file(client_sock, filepath, false);
}

void http_head_response(int client_sock, const char* filepath) {
    http_send_file(client_sock, filepath, true);
}

void http_post_response(int client_sock, const char* data, size_t length) {
    char header[BUF_SIZE];
    snprintf(header, BUF_SIZE,
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/plain\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n",
             length);

    // 发送响应头
    if (send(client_sock, header, strlen(header), 0) < 0) {
        LOG_ERROR("Failed to send POST response header: %s", strerror(errno));
        return;
    }

    LOG_INFO("Sent POST response, data length: %zu", length);
}


const char* http_get_mime_type(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (!dot) {
        return "application/octet-stream";
    }

    for (const struct mime_type* m = mime_types; m->extension; m++) {
        if (strcasecmp(dot, m->extension) == 0) {
            return m->type;
        }
    }
    return "application/octet-stream";
}