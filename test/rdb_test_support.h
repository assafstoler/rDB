/* Small, always-enabled assertions for the user-space contract tests. */
#ifndef RDB_TEST_SUPPORT_H
#define RDB_TEST_SUPPORT_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "rdb.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s: CHECK(%s) failed\n", \
                __FILE__, __LINE__, __func__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)
#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define KEY_OFFSET(type, member, indexes) \
    (offsetof(type, member) - sizeof(rdb_bpp_t) * (indexes))

static inline void *checked_calloc(size_t count, size_t size)
{
    void *result = calloc(count, size);
    CHECK(result != NULL);
    return result;
}

typedef void (*test_function)(void);
struct test_case { const char *name; test_function run; };

/* One CTest process per case: a failure cannot contaminate another fixture. */
static inline int run_case(int argc, char **argv,
                          const struct test_case *cases, size_t count)
{
    if (argc == 2) {
        for (size_t i = 0; i < count; ++i) {
            if (strcmp(argv[1], cases[i].name) != 0) continue;
            rdb_init();
            cases[i].run();
            rdb_clean(0);
            CHECK(rdb_error_string == NULL);
            printf("PASS %s\n", cases[i].name);
            return EXIT_SUCCESS;
        }
    }
    fprintf(stderr, "Usage: %s CASE\nAvailable cases:", argv[0]);
    for (size_t i = 0; i < count; ++i) fprintf(stderr, " %s", cases[i].name);
    fputc('\n', stderr);
    return EXIT_FAILURE;
}

#endif
