#include <stdio.h>  //printf,
#include <stdlib.h> //exit,
#include <string.h>
#include <math.h>   // pow
#include <unistd.h> // getopt
#include <pthread.h>
#include <unistd.h>
#include <getopt.h>
#include "rDB.h"
#include "../utils.h"
#include "../log.h"


uint32_t log_level = LOG_WARN;

pthread_mutex_t  log_mutex;
FILE *logger = NULL;


int main(int argc, char *argv[]) {

    int opt;
    int test=-1;
    struct timespec from = {0,0};
    struct timespec to = {0,0};
    struct timespec res;
    char *ptr=NULL, *sec=NULL, *nsec=NULL;
    
    logger = stdout;

    pthread_mutex_init(&log_mutex, NULL);

    while ((opt = getopt(argc, argv, "t:f:o:")) != -1) {
        switch (opt) {
        case 't':
            test = atoi(optarg);
            break;

        case 'o':
            sec = optarg;
            ptr = strchr(optarg, '.');
            if (ptr) {
               nsec = ptr + 1;
               *ptr = 0;
            } 
            else {
                ptr = NULL;
                nsec = NULL;
            }
            to.tv_sec = atoi(sec);
            if (nsec) {
                to.tv_nsec = /*1000000000. /*/  atoi(nsec);// / (floor(log10(abs(atoi(nsec)))) + 1);
            } 
            else {
                to.tv_nsec = 0;
            }
            break;
        case 'f':
            sec = optarg;
            ptr = strchr(optarg, '.');
            if (ptr) {
               nsec = ptr + 1;
               *ptr = 0;
            } 
            else {
                ptr = NULL;
                nsec = NULL;
            }
            from.tv_sec = atoi(sec);
            if (nsec) {
                //from.tv_nsec = 1000000000. / atoi(nsec);
                from.tv_nsec = /*1000000000. /*/  atoi(nsec);// / (floor(log10(abs(atoi(nsec)))) + 1);
            } 
            else {
                from.tv_nsec = 0;
            }
            break;
        default:
            info("unknown argument: Usage rdb_test -t<n>\n"
                "Possibe tests:\n"
                "1) test init/close\n"
                "2) test init/define pools/close\n");
            break;
        }
    }

    // Getting things started...
    
    if ( test == 1) {

        char buffer[32];
        int64_t ms;

        s_ts_diff_time_ns (&from, &to, &res);

        snprint_ts_time (&res, buffer, 32);
        printf("%s\n", buffer);
        printf("%" PRIi64 "\n", ms = ts_to_ms (&res));
        ms_to_ts (ms, &res);
        ms = ts_to_ms (&res);
        ms_to_ts (ms, &res);
        printf("%" PRIi64 "\n", ms = ts_to_ms (&res));
            
        
    }
       
    if ( test == 2) { 
        int64_t ms;
        s_ts_diff_time_ns (&from, &to, &res);
        clock_prep_abstime ( &res, 10000000, 0 ); // advance by 1/100 sec
        printf("%" PRIi64 "\n", ms = ts_to_ms (&res));
        
        s_ts_diff_time_ns (&from, &to, &res);
        clock_prep_abstime ( &res, -10000000, 0 ); // advance by 1/100 sec
        printf("%" PRIi64 "\n", ms = ts_to_ms (&res));
    }

    if ( test == 3 ) {
        // diff_time_ns
        printf("%d%d%d%d\n",
                is_ts_greater (&from, &to),
                is_ts_greater_equal (&from, &to),
                is_ts_lesser (&from, &to),
                is_ts_lesser_equal (&from, &to));

    }

    exit(0);
}


