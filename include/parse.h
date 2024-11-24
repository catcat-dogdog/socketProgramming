#ifndef PARSE_H
#define PARSE_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SUCCESS 0
// 在头文件中添加缓冲区大小定义
#define MAX_BUFFER_SIZE 8192
#define MAX_HEADER_NAME_LEN 256
#define MAX_HEADER_VALUE_LEN 8192

//Header field
typedef struct
{
	char header_name[4096];
	char header_value[4096];
} Request_header;

//HTTP Request Header
typedef struct
{
	char http_version[50];
	char http_method[50];
	char http_uri[4096];
	Request_header *headers;
	int header_count;
	int header_capacity; // 当前headers的最大容量
} Request;

// typedef struct {
//     char *parsing_buf;
//     int parsing_offset;
//     size_t parsing_buf_siz;
//     Request *parsing_request;
//     // 其他需要的状态变量
// } ParserState;

Request* parse(char *buffer, int size);
//从buffer[0:size-1]解析出第一个Request

// functions decalred in parser.y
int yyparse();
void set_parsing_options(char *buf, size_t i, Request *request);
void free_request(Request *request);

#endif