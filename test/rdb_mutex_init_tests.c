#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rdb.h"

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

int __real_pthread_mutex_init(pthread_mutex_t *mutex,
        const pthread_mutexattr_t *attr);
int __real_pthread_mutex_destroy(pthread_mutex_t *mutex);

static int watch_mutexes;
static int fail_mutex_init;
static int mutex_error;
static int mutex_attempts;
static int mutex_successes;
static int mutex_destroys;
static uintptr_t initialized_mutexes[2];

int __wrap_pthread_mutex_init(pthread_mutex_t *mutex,
        const pthread_mutexattr_t *attr)
{
    int rc;
    if (watch_mutexes) {
        ++mutex_attempts;
        if (fail_mutex_init && --fail_mutex_init == 0)
            return mutex_error;
    }
    rc = __real_pthread_mutex_init(mutex, attr);
    if (watch_mutexes && rc == 0) {
        CHECK(mutex_successes < 2);
        if (mutex_successes < 2)
            initialized_mutexes[mutex_successes++] = (uintptr_t)mutex;
    }
    return rc;
}

int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    int found = -1;
    if (watch_mutexes) {
        for (int i = 0; i < mutex_successes; ++i)
            if (initialized_mutexes[i] == (uintptr_t)mutex) found = i;
        CHECK(found >= 0);
        if (found >= 0) initialized_mutexes[found] = 0;
        ++mutex_destroys;
    }
    return __real_pthread_mutex_destroy(mutex);
}

typedef struct {
    rdb_bpp_t link;
    int32_t key;
} record_t;

typedef rdb_pool_t *(*create_pool_fn)(char *, int, int, int, void *, size_t);

static void keep_record(void *record, void *unused)
{
    (void)record;
    (void)unused;
}

static void check_failure(create_pool_fn create, const char *api,
        int failed_init, rdb_pool_t *kept, record_t *record)
{
    char name[64];
    unsigned char kept_before[sizeof(*kept)];
    rdb_pool_t *failed, *retry;
    const char *failed_lock = failed_init == 1 ? "read mutex" : "write mutex";
    int key_offset = (int)offsetof(record_t, key) - (int)sizeof(rdb_bpp_t);

    snprintf(name, sizeof(name), "%s-failed-%d", api, failed_init);
    memcpy(kept_before, kept, sizeof(*kept));
    mutex_attempts = 0;
    mutex_successes = 0;
    mutex_destroys = 0;
    fail_mutex_init = failed_init;
    mutex_error = EAGAIN;
    memset(initialized_mutexes, 0, sizeof(initialized_mutexes));

    watch_mutexes = 1;
    failed = create(name, 1, key_offset, RDB_BTREE | RDB_KINT32, NULL, 0);
    watch_mutexes = 0;

    CHECK(failed == NULL);
    CHECK(mutex_attempts == failed_init);
    CHECK(mutex_successes == failed_init - 1);
    CHECK(mutex_destroys == failed_init - 1);
    CHECK(fail_mutex_init == 0);
    CHECK(initialized_mutexes[0] == 0 && initialized_mutexes[1] == 0);
    CHECK(rdb_find_pool_by_name(name) == NULL);
    CHECK(rdb_find_pool_by_name("mutex-kept") == kept);
    CHECK(memcmp(kept_before, kept, sizeof(*kept)) == 0);
    CHECK(rdb_get_const(kept, 0, record->key) == record);
    CHECK(rdb_error_string && strstr(rdb_error_string, failed_lock));
    if (rdb_error_string) {
        char code[24];
        snprintf(code, sizeof(code), "(%d)", EAGAIN);
        CHECK(strstr(rdb_error_string, code) != NULL);
    }

    retry = create(name, 1, key_offset, RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(retry != NULL);
    CHECK(rdb_find_pool_by_name(name) == retry);
    if (retry) {
        CHECK(pthread_mutex_trylock(&retry->read_mutex) == 0);
        CHECK(pthread_mutex_unlock(&retry->read_mutex) == 0);
        CHECK(pthread_mutex_trylock(&retry->write_mutex) == 0);
        CHECK(pthread_mutex_unlock(&retry->write_mutex) == 0);
        rdb_drop_pool(retry);
    }
    CHECK(memcmp(kept_before, kept, sizeof(*kept)) == 0);
    CHECK(rdb_get_const(kept, 0, record->key) == record);
}

int main(void)
{
    create_pool_fn create[] = {rdb_add_pool, rdb_register_um_pool};
    const char *api_names[] = {"add", "register"};
    int key_offset = (int)offsetof(record_t, key) - (int)sizeof(rdb_bpp_t);
    rdb_pool_t *kept;
    record_t *record;

    rdb_init();
    kept = rdb_register_um_pool("mutex-kept", 1, key_offset,
            RDB_BTREE | RDB_KINT32, NULL, 0);
    CHECK(kept != NULL);
    record = calloc(1, sizeof(*record));
    CHECK(record != NULL);
    if (!kept || !record) goto done;
    record->key = 17;
    CHECK(rdb_insert(kept, record) == 1);

    for (size_t api = 0; api < sizeof(create) / sizeof(create[0]); ++api)
        for (int failed_init = 1; failed_init <= 2; ++failed_init)
            check_failure(create[api], api_names[api], failed_init,
                    kept, record);

done:
    if (kept) rdb_flush(kept, keep_record, NULL);
    if (kept) rdb_drop_pool(kept);
    free(record);
    rdb_clean(0);
    if (failures) return EXIT_FAILURE;
    puts("Ok");
    return EXIT_SUCCESS;
}
