//Copyright (c) 2014-2020 Assaf Stoler <assaf.stoler@gmail.com>
//All rights reserved.
//see LICENSE for more info

#ifndef __LOG_H
#define __LOG_H
#include <pthread.h>
#include <inttypes.h>

#ifdef BUILDING_LIB
#include "model_cpp_interface.h"
#include "messaging.h"
#include "rdbfw.h"
#include "utils.h"
#include "ansi.h"
#else
#include <rdbfw/model_cpp_interface.h>
#include <rdbfw/messaging.h>
#include <rdbfw/rdbfw.h>
#include <rdbfw/utils.h>
#include <rdbfw/ansi.h>
#endif

#include <stdio.h>

extern uint32_t log_level;
extern pthread_mutex_t  log_mutex;

extern char log_log_buf[256];
extern char sig_log_buf[256];
//static char log_log_buf[256];
//static char sig_log_buf[256];

//To have more human-readable timestamp. Best if user reset to current time
static const int64_t reducer = 1563000000;

// log level of 0 is quite mode...
#define LOG_FATAL 1
#define LOG_ERROR 2
#define LOG_WARN  3
#define LOG_INFO  4
#define LOG_DEBUG 5
#define LOG_DEBUG_MORE 6
#define LOG_TRACE 7

//Higher values are reserved! (used by log-level)
#define DF_RDB   ( 1 << 3 )
#define DF_RDBFW ( 1 << 4 )
#define DF_UT    ( 1 << 5 ) // UNITTEST

#define LEVELS 0x7
#define LOG_FLAGS 0xFFFFFFF8
#define DEBUG_FLAGS ( 0 )


#define fwlog_no_emit fwlog
#define fwlog(a, b, arg...)  do {    \
        if (log_level >= a) {       \
            pthread_mutex_lock(&log_mutex); \
            fprintf(stdout, "%" PRIi64 ":%20s:_%d_:", clock_gettime_ms(NULL), __FUNCTION__, a); \
            fprintf(stdout,b,##arg);    \
            pthread_mutex_unlock(&log_mutex); \
        }           \
    } while (0)

#define sigfwlog(a, b, arg...)  do {    \
        if ( log_level >= (a) && 3 >= (a) ) { \
            snprintf(sig_log_buf, 255, "%c%'.3f:%20s:_%d_:"b, ((a)<=2) ? 'e' : ' ', (double) (clock_gettime_ms(NULL) / 1000), __FUNCTION__, (a),##arg); \
            rdbmsg_emit_log (rdbmsg_lookup_id("ROUTE_LOGGER"), rdbmsg_lookup_id("ROUTE_LOGGER"), rdbmsg_lookup_id("GROUP_LOGGER"), rdbmsg_lookup_id("ID_LOG_ENTRY"), 255, sig_log_buf, 1); \
            fprintf ( logger, "%s", sig_log_buf ); \
        } \
        else if ( log_level >= (a) ) {       \
            fprintf(logger, " %'.3f:%20s:_%d_:",(double) (clock_gettime_ms(NULL) / 1000), __FUNCTION__, (a)); \
            fprintf(logger,b,##arg);    \
        }           \
    } while (0)

#define fwl(a, b, c, arg...) do {    \
        if ( ( log_level >= ((a) & LEVELS) ) || ( log_level > 0 && ( ( ( DEBUG_FLAGS ) & ( (a) & LOG_FLAGS ) ) != 0 ) ) ) {  \
            if ( 3 >= ((a) & LEVELS)  && ( 0 == ( ( ( a ) & LOG_FLAGS )  & DF_UT ) ) ) { \
                pthread_mutex_lock(&log_mutex); \
                snprintf(log_log_buf, sizeof(log_log_buf), "%c%'.3f:%15s:%20s:_%d_:" c, (((a) & LEVELS)<=2) ? 'e' : ' ', clock_gettime_ms(NULL) / 1000. - reducer, (b == NULL) ? "" : ((plugins_t *)b)->uname, __FUNCTION__, ((a) & LEVELS),##arg); \
                if ( (a) & DF_UT ) { fprintf ( logger, "%s", ANSI_COLOR_BLUE ); } \
                else if ( ((a) & LEVELS ) <= 2 ) { fprintf ( logger, "%s", ANSI_COLOR_RED ); } \
                else if ( ((a) & LEVELS ) == 3 ) { fprintf ( logger, "%s", ANSI_COLOR_YELLOW ); } \
                else if ( ((a) & LEVELS ) == 4 ) { fprintf ( logger, "%s", ANSI_COLOR_GREEN ); } \
                fprintf ( logger, "%s", log_log_buf ); \
                fprintf ( logger, "%s", ANSI_COLOR_RESET ); \
                rdbmsg_emit_log (rdbmsg_lookup_id("ROUTE_LOGGER"), rdbmsg_lookup_id("ROUTE_LOGGER"), rdbmsg_lookup_id("GROUP_LOGGER"), rdbmsg_lookup_id("ID_LOG_ENTRY"), 255, log_log_buf, 1); \
                /*pthread_mutex_unlock(&log_mutex);*/ \
            } \
            else  {       \
                pthread_mutex_lock(&log_mutex); \
                if ( (a) & DF_UT ) { fprintf ( logger, "%s", ANSI_COLOR_BLUE ); } \
                else if ( (a) <= 2 ) { fprintf ( logger, "%s", ANSI_COLOR_RED ); } \
                else if ( (a) == 3 ) { fprintf ( logger, "%s", ANSI_COLOR_YELLOW ); } \
                else if ( (a) == 4 ) { fprintf ( logger, "%s", ANSI_COLOR_GREEN ); } \
                fprintf(logger, "%c%'.3f:%15s:%20s:_%d_:" c, ( ( ( ( a ) & LOG_FLAGS ) ) & DF_UT ) ? '*' : ' ', (double) ((int64_t) clock_gettime_ms(NULL) / 1000. - reducer), (b == NULL) ? "" : ((plugins_t *)b)->uname, __FUNCTION__, ((a) & LEVELS) ,##arg); \
                fprintf ( logger, "%s", ANSI_COLOR_RESET ); \
                pthread_mutex_unlock(&log_mutex); \
            }           \
        } \
    } while (0)

#define fwl_no_emit(a, b, c, arg...) do {    \
        if ( ( log_level >= ((a) & LEVELS) ) || ( log_level > 0 && ( ( ( DEBUG_FLAGS ) & ( (a) & LOG_FLAGS ) ) != 0 ) ) ) {  \
            if ( 3 >= ((a) & LEVELS)  && ( 0 == ( ( ( a ) & LOG_FLAGS )  & DF_UT ) ) ) { \
                pthread_mutex_lock(&log_mutex); \
                snprintf(log_log_buf, sizeof(log_log_buf), "%c%'.3f:%15s:%20s:_%d_:" c, (((a) & LEVELS)<=2) ? 'e' : ' ', clock_gettime_ms(NULL) / 1000. - reducer, (b == NULL) ? "" : ((plugins_t *)b)->uname, __FUNCTION__, ((a) & LEVELS),##arg); \
                if ( (a) & DF_UT ) { fprintf ( logger, "%s", ANSI_COLOR_BLUE ); } \
                else if ( ((a) & LEVELS ) <= 2 ) { fprintf ( logger, "%s", ANSI_COLOR_RED ); } \
                else if ( ((a) & LEVELS ) == 3 ) { fprintf ( logger, "%s", ANSI_COLOR_YELLOW ); } \
                else if ( ((a) & LEVELS ) == 4 ) { fprintf ( logger, "%s", ANSI_COLOR_GREEN ); } \
                fprintf ( logger, "%s", log_log_buf ); \
                fprintf ( logger, "%s", ANSI_COLOR_RESET ); \
                pthread_mutex_unlock(&log_mutex); \
            } \
            else  {       \
                pthread_mutex_lock(&log_mutex); \
                if ( (a) & DF_UT ) { fprintf ( logger, "%s", ANSI_COLOR_BLUE ); } \
                else if ( (a) <= 2 ) { fprintf ( logger, "%s", ANSI_COLOR_RED ); } \
                else if ( (a) == 3 ) { fprintf ( logger, "%s", ANSI_COLOR_YELLOW ); } \
                else if ( (a) == 4 ) { fprintf ( logger, "%s", ANSI_COLOR_GREEN ); } \
                fprintf(logger, "%c%'.3f:%15s:%20s:_%d_:" c, ( ( ( ( a ) & LOG_FLAGS ) ) & DF_UT ) ? '*' : ' ', (double) ((int64_t) clock_gettime_ms(NULL) / 1000. - reducer), (b == NULL) ? "" : ((plugins_t *)b)->uname, __FUNCTION__, ((a) & LEVELS) ,##arg); \
                fprintf ( logger, "%s", ANSI_COLOR_RESET ); \
                pthread_mutex_unlock(&log_mutex); \
            }           \
        } \
    } while (0)


#endif
