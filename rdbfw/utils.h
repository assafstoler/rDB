#ifndef _UTILS_H_
#define _UTILS_H_

uint64_t diff_time_ns(struct timespec *from, struct timespec *to,
                   struct timespec *res);

void print_time (struct timespec *ts);

int64_t clock_gettime_ms(struct timespec *res);

int64_t ts_to_ms(struct timespec *ts);
void ms_to_ts (int64_t ms, struct timespec *res);

int64_t s_ts_diff_time_ns(struct timespec *from, struct timespec *to, struct timespec *res);

#endif
