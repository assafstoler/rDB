#include <inttypes.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "utils.h"

/* Calculate and redurn the time delta in nano-seconds, between two supplied
 * timespec structures (from, to).
 *
 * Result is returned as a 64 bit integer, as well as set into the
 * result (*res) pointer, if suppplied.
 *
 * on error (null input), zero is returned, and *res is unchanged
 */


#define DIVIDER_1  1000000000
#define SEC_TO_MSEC 1000
#define NSEC_TO_MSEC 1000000


int64_t clock_gettime_ms(struct timespec *res) {
    struct timespec ts;
    if (res == NULL) {
        res = &ts;
    }
    if (-1 != clock_gettime(CLOCK_REALTIME, res)) {
        return ts_to_ms(res);
    } else {
        return -1;
    }
}

int64_t ts_to_ms(struct timespec *ts) {
    if (!ts) {
        return 0;
    }
    
    if (ts->tv_sec < 0 || ts->tv_nsec < 0) {
        return (-1 * (((int64_t) labs(ts->tv_sec) * SEC_TO_MSEC) + (labs(ts->tv_nsec) / NSEC_TO_MSEC)));
    }
    else {
        return (((int64_t) ts->tv_sec * SEC_TO_MSEC) + (ts->tv_nsec / NSEC_TO_MSEC));
    }
}

//UnitTested
void ms_to_ts (int64_t ms, struct timespec *res) {
    if (res != NULL) {
        res->tv_sec = (ms / SEC_TO_MSEC) ;//* ((ms < 0) ? -1 : 1);
        res->tv_nsec = (ms % SEC_TO_MSEC) * NSEC_TO_MSEC;// * ((res->tv_sec == 0 && (ms < 0)) ? -1 : 1);
    }
}

/*** clock_prep_abstime prepare a timer loaded with experation date in the future
 *   for usage by appropriate functions / fimeouts
 *
 *   Able to handle all signess conbinations
 *
 *   Result is stored in to ts 
 **/
//unitTested
int clock_prep_abstime(struct timespec *ts, int64_t delta, int read_clock) {
    if (!read_clock || (-1 != clock_gettime(CLOCK_REALTIME, ts))) {
        if (ts->tv_sec >=0 && ts->tv_nsec >=0) {
            ts->tv_nsec += delta;
            while ( ts->tv_nsec >= 1000000000 ) {
                ts->tv_sec ++;
                ts->tv_nsec -= 1000000000;
            }
            while ( ts->tv_nsec < 0 ) {
                ts->tv_sec --;
                ts->tv_nsec += 1000000000;
            }
        } else { //Negative
            ts->tv_nsec = labs(ts->tv_nsec) - delta;
            ts->tv_sec = labs(ts->tv_sec);

            while ( ts->tv_nsec >= 1000000000 ) {
                ts->tv_sec ++;
                ts->tv_nsec -= 1000000000;
            }
            while ( ts->tv_nsec < 0 ) {
                ts->tv_sec --;
                ts->tv_nsec += 1000000000;
            }

            if (ts->tv_sec) {
                ts->tv_sec *= -1;
            } else {
                ts->tv_nsec *= -1;
            }
        }

        return 0;
    } else {
        return -1;
    }
}
int is_ts_greater(struct timespec *subject, struct timespec *challenger) {
    if (subject->tv_sec < challenger->tv_sec) {
        return 1;
    }
    if (subject->tv_sec == challenger->tv_sec && subject->tv_nsec < challenger->tv_nsec) {
        return 1;
    }
    else {
        return 0;
    }
}
// UnitTested
int is_ts_greater_equal(struct timespec *subject, struct timespec *challenger) {
    if (subject->tv_sec < challenger->tv_sec) {
        return 1;
    }
    if (subject->tv_sec == challenger->tv_sec && subject->tv_nsec <= challenger->tv_nsec) {
        return 1;
    }
    else {
        return 0;
    }
}
// UnitTested
int is_ts_lesser(struct timespec *subject, struct timespec *challenger) {
    if (subject->tv_sec > challenger->tv_sec) {
        return 1;
    }
    if (subject->tv_sec == challenger->tv_sec && subject->tv_nsec > challenger->tv_nsec) {
        return 1;
    }
    else {
        return 0;
    }
}
// UnitTested
int is_ts_lesser_equal(struct timespec *subject, struct timespec *challenger) {
    if (subject->tv_sec > challenger->tv_sec) {
        return 1;
    }
    if (subject->tv_sec == challenger->tv_sec && subject->tv_nsec >= challenger->tv_nsec) {
        return 1;
    }
    else {
        return 0;
    }
}

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

// print at most len-1 characters of time into string
// *
char *snprint_ts_time (struct timespec *ts, char *string, int len) 
{
    //int mult = 1;
    int used = 0;
    char *ptr;

    if (len < 2) {
        *string = 0;
        return NULL;
    }

    if (ts->tv_sec < 0 || (ts->tv_sec == 0 && ts->tv_nsec < 0)) {
        snprintf(string,2,"-");
        used = 1;
        //mult = -1;
    } 
    else {
       *string = 0;
       used = 0;
    }

    ptr = string + used;

    snprintf(ptr, len - used, "%'ld.%09ld",
           labs(ts->tv_sec) /** mult*/,
           labs(ts->tv_nsec) /** mult*/);
    return string;
}
