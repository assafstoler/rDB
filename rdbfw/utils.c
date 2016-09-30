#include <inttypes.h>
#include <time.h>
#include <stdio.h>

/* Calculate and redurn the time delta in nano-seconds, between two supplied
 * timespec structures (from, to).
 *
 * Result is returned as a 64 bit integer, as well as set into the
 * result (*res) pointer, if suppplied.
 *
 * on error (null input), zero is returned, and *res is unchanged
 */

uint64_t diff_time_ns(struct timespec *from, struct timespec *to,
                   struct timespec *res)
{
    struct timespec tmp_time;

    if (res == NULL) {
        res = &tmp_time;
    }

    if (from == NULL || to == NULL) {
        return 0LL;
    }

    res->tv_sec = to->tv_sec - from->tv_sec;

    if (to->tv_nsec > from->tv_nsec) {
        res->tv_nsec = to->tv_nsec - from->tv_nsec ;
    } else {
        res->tv_nsec = to->tv_nsec - from->tv_nsec  + 1000000000;
        res->tv_sec --;
    }

    return (uint64_t) res->tv_sec *  1000000000LL + res->tv_nsec;
}

/* print time to stdout
 *
 */
void print_time (struct timespec *ts)
{
    printf("%'ld.%09ld\n",
           (long) ts->tv_sec,
           ts->tv_nsec);
}
