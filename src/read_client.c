#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define MAX_REQUEST_SIZE 4096
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9999

// 从文件中读取一个完整的HTTP请求
char *read_request(FILE *fp, char *buffer)
{
    char line[1024];
    int pos = 0;

    // 清空buffer
    memset(buffer, 0, MAX_REQUEST_SIZE);

    // 逐行读取，直到遇到空行
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        // 将行添加到buffer中
        strcat(buffer + pos, line);
        pos += strlen(line);

        // 防止buffer溢出
        if (pos >= MAX_REQUEST_SIZE - 1)
        {
            break;
        }

        // 检查是否是空行（只包含换行符）
        if (line[0] == '\n' || (line[0] == '\r' && line[1] == '\n'))
        {
            break;
        }
    }

    // 如果没有读取到任何内容，返回NULL
    return (pos > 0) ? buffer : NULL;
}

int main()
{
    int sock_fd;
    struct sockaddr_in server_addr;
    char request_buffer[MAX_REQUEST_SIZE];
    FILE *fp;

    // 创建socket
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket创建失败");
        exit(1);
    }

    // 设置服务器地址结构
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0)
    {
        perror("地址转换失败");
        exit(1);
    }

    // 连接服务器
    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("连接失败");
        exit(1);
    }

    // 打开请求文件
    fp = fopen("samples/request_pipeline", "r");
    if (fp == NULL)
    {
        perror("文件打开失败");
        close(sock_fd);
        exit(1);
    }

    // 读取并发送每个请求
    while (read_request(fp, request_buffer) != NULL)
    {
        printf("\n发送请求:\n");

        // 发送请求到服务器
        if (send(sock_fd, request_buffer, strlen(request_buffer), 0) < 0)
        {
            perror("发送失败");
            break;
        }

        // 接收服务器响应（可选）
        char response[4096];
        memset(response, 0, sizeof(response));
        int bytes_received = recv(sock_fd, response, sizeof(response) - 1, 0);
        if (bytes_received > 0)
        {
            printf("\n收到响应:\n%s\n", response);
        }

        // 添加延迟，避免请求发送过快
        usleep(500000); // 500ms延迟
    }

    // 清理资源
    fclose(fp);
    close(sock_fd);
    return 0;
}
