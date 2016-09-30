#ifndef _UTILS_H_
#define _UTILS_H_

uint64_t diff_time_ns(struct timespec *from, struct timespec *to,
                   struct timespec *res);

void print_time (struct timespec *ts);

#endif
