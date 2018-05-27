#ifndef COMMON_H
#define COMMON_H

#include <time.h>
#include <stdio.h>
#include <stdlib.h>

// Terminology according to
// https://en.wikipedia.org/wiki/ANSI_escape_code#Sequence_elements
#define ESC "\033"
#define CSI ESC "["

#define CYAN    CSI"1;36m"
#define GREEN   CSI"0;32m"
#define RED     CSI"1;31m"
#define ORANGE  CSI"1;33m"
#define MAGENTA CSI"1;35m"
#define NORMAL  CSI"0m"

#define STR(x) #x

static inline const char *timestamp(char *s, size_t max)
{
    time_t t = time(NULL);
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(s, max, "%T", &tm);
    return s;
}

/**
 * @brief 报告系统错误，并退出程序
 * @param msg 传递给 perror 的消息字符串
 */
#define sys_panic(msg)          \
    do {                        \
        perror(RED msg NORMAL); \
        exit(EXIT_FAILURE);     \
    } while (0)

#define panic(fmt, ...)                                                               \
    do {                                                                              \
        char __s[32];                                                                 \
        fprintf(stderr, RED "[%s,%s:%d] " fmt NORMAL "\n",                            \
                timestamp(__s, sizeof(__s)), __FUNCTION__, __LINE__, ## __VA_ARGS__); \
        exit(EXIT_FAILURE);                                                           \
    } while (0)

#define log(fmt, ...)                                                                 \
    do {                                                                              \
        char __s[32];                                                                 \
        fprintf(stderr, GREEN "[%s,%s:%d] " NORMAL fmt "\n",                          \
                timestamp(__s, sizeof(__s)), __FUNCTION__, __LINE__, ## __VA_ARGS__); \
    } while (0)

#define warn(fmt, ...)                                                                \
    do {                                                                              \
        char __s[32];                                                                 \
        fprintf(stderr, RED "[%s,%s:%d] " NORMAL fmt "\n",                            \
                timestamp(__s, sizeof(__s)), __FUNCTION__, __LINE__, ## __VA_ARGS__); \
    } while (0)

#define Assert(expr, fmt, ...)                                          \
    do {                                                                \
        if (!(expr)) {                                                  \
            panic("\"" STR(expr) "\"" " failed: " fmt, ## __VA_ARGS__); \
        }                                                               \
    } while (0)

#define print(fmt, ...)                                                                \
    do {                                                                              \
        char __s[32];                                                                 \
        printf(GREEN "[%s:%d] " NORMAL fmt ,                            \
                 __FUNCTION__, __LINE__, ## __VA_ARGS__); \
    } while (0)
#endif // COMMON_H