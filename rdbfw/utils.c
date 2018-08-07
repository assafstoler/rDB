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

int64_t s_ts_diff_time_ns(struct timespec *from, struct timespec *to,
                   struct timespec *res)
{
    struct timespec tmp_time;
    struct timespec tmp_to;
    int from_sign;
    int to_sign;

    if (res == NULL) {
        res = &tmp_time;
    }

    if (to == NULL) {
        to = &tmp_to;
        if(-1 == clock_gettime(CLOCK_REALTIME, &tmp_to)) {
            return 0LL;
        }
    }

    if (from == NULL) {
        return 0LL;
    }

    if (from->tv_sec < 0 /*|| from->tv_nsec < 0*/) {
        from_sign = -1;
    }
    else {
        from_sign = 1;
    }
    if (to->tv_sec < 0/* || to->tv_nsec < 0*/) {
        to_sign = -1;
    }
    else {
        to_sign = 1;
    }

    if (to->tv_sec > from->tv_sec ||
            (to->tv_sec == from->tv_sec && to->tv_nsec >= from->tv_nsec)) {
        // positive return

        res->tv_sec = to->tv_sec - from->tv_sec;

        if (/*to_sign */ to->tv_nsec >= /*from_sign */ from->tv_nsec) {
            res->tv_nsec = (to_sign * to->tv_nsec) - (from_sign * from->tv_nsec) ;
            while (res->tv_nsec >= 1000000000 ) {
                res->tv_nsec -= 1000000000;
                res->tv_sec++;
            }
        } else {
            res->tv_nsec = (to_sign * to->tv_nsec) - (from_sign * from->tv_nsec)  + 1000000000;
            res->tv_sec --;
            while (res->tv_nsec >= 1000000000 ) {
                res->tv_nsec -= 1000000000;
                res->tv_sec++;
            }
        }
        return (int64_t) res->tv_sec *  1000000000LL + res->tv_nsec;
    } else {
        // Negative return
        res->tv_sec = from->tv_sec - to->tv_sec;

        if (from->tv_nsec >= to->tv_nsec) {
            res->tv_nsec = (from_sign * from->tv_nsec) - (to_sign * to->tv_nsec) ;
            if (res->tv_nsec >= 1000000000 ) {
                res->tv_nsec -= 1000000000;
                res->tv_sec++;
            }
        } else {
            res->tv_nsec = (from_sign * from->tv_nsec) - (to_sign * to->tv_nsec)  + 1000000000;
            res->tv_sec --;
            if (res->tv_nsec >= 1000000000 ) {
                res->tv_nsec -= 1000000000;
                res->tv_sec++;
            }
        }
        if (res->tv_sec) {
            res->tv_sec = res->tv_sec * -1;
        }
        else {
            res->tv_nsec = res->tv_nsec * -1;
        }
        return (int64_t) res->tv_sec *  1000000000LL + res->tv_nsec;
    }
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
