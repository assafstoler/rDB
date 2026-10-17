#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rdb.h"

static int failures;
static int fail_realloc;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

void *__real_realloc(void *ptr, size_t size);

void *__wrap_realloc(void *ptr, size_t size)
{
    if (fail_realloc) {
        fail_realloc = 0;
        return NULL;
    }
    return __real_realloc(ptr, size);
}

int main(void)
{
    rdb_init();
    rdb_error("previous diagnostic");
    char *saved = rdb_error_string;
    CHECK(saved != NULL);

    fail_realloc = 1;
    CHECK(rdb_error_value(-37, "replacement from value API") == -37);
    CHECK(!fail_realloc);
    CHECK(rdb_error_string == saved);
    CHECK(strcmp(rdb_error_string, "previous diagnostic") == 0);

    fail_realloc = 1;
    rdb_error("replacement from void API");
    CHECK(!fail_realloc);
    CHECK(rdb_error_string == saved);
    CHECK(strcmp(rdb_error_string, "previous diagnostic") == 0);

    CHECK(rdb_error_value(23, "successful replacement") == 23);
    CHECK(strcmp(rdb_error_string, "successful replacement") == 0);
    rdb_clean(0);
    CHECK(rdb_error_string == NULL);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
