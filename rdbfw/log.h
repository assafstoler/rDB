#ifndef __LOG_H
#define __LOG_H
#include "utils.h"
#include "inttypes.h"

extern uint32_t log_level;
extern pthread_mutex_t  log_mutex;

// log level of 0 is quite mode...
#define LOG_FATAL 1
#define LOG_ERROR 2
#define LOG_WARN  3
#define LOG_INFO  4
#define LOG_DEBUG 5
#define LOG_DEBUG_MORE 6
#define LOG_TRACE 7

/*#define log(a, b, arg...)  do {    \
        if (log_level >= a) {       \
            fprintf(stdout, "%20s: ",__FUNCTION__); \
            c_info(b,##arg); \
        }           \
    } while (0)
*/
#define fwlog(a, b, arg...)  do {    \
        if (log_level >= a) {       \
            pthread_mutex_lock(&log_mutex); \
            fprintf(stdout, "%"PRIi64":%20s:_%d_:", clock_gettime_ms(NULL), __FUNCTION__, a); \
            fprintf(stdout,b,##arg);    \
            pthread_mutex_unlock(&log_mutex); \
        }           \
    } while (0)

#define sigfwlog(a, b, arg...)  do {    \
        if (log_level >= a) {       \
            fprintf(stdout, "%"PRIi64":%20s:_%d_:", clock_gettime_ms(NULL), __FUNCTION__, a); \
            fprintf(stdout,b,##arg);    \
        }           \
    } while (0)

#endif
