//Copyright (c) 2014-2020 Assaf Stoler <assaf.stoler@gmail.com>
//All rights reserved.
//see LICENSE for more info

#ifndef _UTILS_H_
#define _UTILS_H_

#include <time.h>
#include <stdio.h>

#define CLOCK_GET_TIME 0

void clock_settime_ns(int64_t new_time);
uint64_t clock_gettime_uns(void);
int64_t clock_gettime_us(void);

uint64_t diff_time_ns(struct timespec *from, struct timespec *to,
                   struct timespec *res);

void print_time (struct timespec *ts);

int clock_prep_abstime(struct timespec *ts, int64_t delta, int read_clock);
int64_t clock_gettime_ms(struct timespec *res);



int64_t clock_gettime_ms(struct timespec *res);
void clock_settime_ms(int64_t new_time);
int64_t ts_to_ms(struct timespec *ts);
void ms_to_ts (int64_t ms, struct timespec *res);

int64_t s_ts_diff_time_ns(struct timespec *from, struct timespec *to, struct timespec *res);
int is_ts_greater(struct timespec *subject, struct timespec *challenger);
int is_ts_greater_equal(struct timespec *subject, struct timespec *challenger);
int is_ts_lesser(struct timespec *subject, struct timespec *challenger);
int is_ts_lesser_equal(struct timespec *subject, struct timespec *challenger);
char *snprint_ts_time (struct timespec *ts, char *string, int len);
int fd_set_flag(int fd, int flag, int set);

char *hex_log(uint8_t *ptr, int len);
#define FLAG_SET 1
#define FLAG_UNSET 0
#endif
