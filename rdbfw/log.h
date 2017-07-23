#ifndef __LOG_H
#define __LOG_H

extern uint32_t log_level;

// log level of 0 is quite mode...
#define LOG_FATAL 1
#define LOG_ERROR 2
#define LOG_WARN  3
#define LOG_INFO  4
#define LOG_DEBUG 5
#define LOG_DEBUG_MORE 6
#define LOG_TRACE 7

#define log(a, b, arg...)  do {    \
        if (log_level >= a) {       \
            fprintf(stdout, "%20s: ",__FUNCTION__); \
            c_info(b,##arg); \
        }           \
    } while (0)

#endif
