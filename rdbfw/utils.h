#ifndef _UTILS_H_
#define _UTILS_H_

uint64_t diff_time_ns(struct timespec *from, struct timespec *to,
                   struct timespec *res);

void print_time (struct timespec *ts);

int clock_prep_abstime(struct timespec *ts, int64_t delta, int read_clock);
int64_t clock_gettime_ms(struct timespec *res);


int64_t ts_to_ms(struct timespec *ts);
void ms_to_ts (int64_t ms, struct timespec *res);

int64_t s_ts_diff_time_ns(struct timespec *from, struct timespec *to, struct timespec *res);
int is_ts_greater(struct timespec *subject, struct timespec *challenger);
int is_ts_greater_equal(struct timespec *subject, struct timespec *challenger);
int is_ts_lesser(struct timespec *subject, struct timespec *challenger);
int is_ts_lesser_equal(struct timespec *subject, struct timespec *challenger);
char *snprint_ts_time (struct timespec *ts, char *string, int len);

#endif
