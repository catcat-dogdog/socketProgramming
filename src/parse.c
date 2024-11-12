#include "parse.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define default_header_capacity 16
extern FILE *yyin;

/**
* Given a char buffer returns the parsed request headers
*/
Request * parse(char *buffer, int size) {
    // Different states in the state machine
    enum {
        STATE_START = 0, STATE_CR, STATE_CRLF, STATE_CRLFCR, STATE_CRLFCRLF
    };

    int i = 0, state;
    size_t offset = 0;
    char ch;
    char buf[8192];
    memset(buf, 0, 8192);

    state = STATE_START;
    while (state != STATE_CRLFCRLF) {
        char expected = 0;

        if (i == size)
            break;

        ch = buffer[i++];
        buf[offset++] = ch;

        switch (state) {
        case STATE_START:
        case STATE_CRLF:
            expected = '\r';
            break;
        case STATE_CR:
        case STATE_CRLFCR:
            expected = '\n';
            break;
        default:
            state = STATE_START;
            continue;
        }

        if (ch == expected)
            state++;
        else
            state = STATE_START;

    }

    // Valid End State
    if (state == STATE_CRLFCRLF) {
        Request *request = (Request *) malloc(sizeof(Request));
        request->header_count = 0;
        request->header_capacity = default_header_capacity;
        request->headers = (Request_header *) malloc(sizeof(Request_header) * request->header_capacity);

        // 设置解析选项
        set_parsing_options(buf, i, request);

        // 解析 HTTP 方法行
        if (yyparse() == SUCCESS) {
            // 检查是否为已实现的方法
            if (strcmp(request->http_method, "GET") == 0 || 
                strcmp(request->http_method, "HEAD") == 0 || 
                strcmp(request->http_method, "POST") == 0) {
                return request;
            } else {
                // 未实现的方法
                return request;
            }
        } else {
            yyrestart(yyin); // 重置输入文件
            free(request->headers);
            free(request);
        }
    }

    // 请求格式错误
    printf("Parsing Failed\n");
    return NULL;
}
