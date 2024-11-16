#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <time.h>
#include <stdarg.h>

// 日志级别
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} log_level_t;

// 日志初始化函数
int log_init(const char* log_file_path);

// 日志写入函数
void log_write(log_level_t level, const char* file, int line, const char* fmt, ...);

// 日志关闭函数
void log_close(void);

// 日志宏定义，方便使用
#define LOG_DEBUG(fmt, ...) log_write(LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  log_write(LOG_INFO,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  log_write(LOG_WARN,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_write(LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif
