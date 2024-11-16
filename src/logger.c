#include "logger.h"
#include <string.h>
#include <sys/time.h>
#include <pthread.h>

static FILE* log_file = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char* level_names[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR"
};

int log_init(const char* log_file_path) {
    log_file = fopen(log_file_path, "a");
    if (log_file == NULL) {
        return -1;
    }
    return 0;
}

void log_write(log_level_t level, const char* file, int line, const char* fmt, ...) {
    if (log_file == NULL) return;

    time_t now;
    time(&now);
    struct tm* local_time = localtime(&now);
    
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", local_time);

    // 准备日志消息
    char log_msg[4096];
    char content[2048];
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(content, sizeof(content), fmt, args);
    va_end(args);

    snprintf(log_msg, sizeof(log_msg), "[%s][%s][%s:%d] %s\n",
             time_str, level_names[level], file, line, content);

    pthread_mutex_lock(&log_mutex);
    
    // 写入文件
    fputs(log_msg, log_file);
    fflush(log_file);
    
    // 同时输出到控制台
    // ERROR 级别使用 stderr，其他级别使用 stdout
    if (level == LOG_ERROR) {
        fputs(log_msg, stderr);
        fflush(stderr);
    } else {
        fputs(log_msg, stdout);
        fflush(stdout);
    }
    
    pthread_mutex_unlock(&log_mutex);
}

void log_close(void) {
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
} 