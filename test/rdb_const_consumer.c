/* Compile as C and C++ without prerequisite headers or a caller-supplied
 * USE_128_BIT_TYPES flag. */
#include "rdb.h"
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(condition) do { \
    if (!(condition)) { fprintf(stderr, "consumer ABI check failed: %s\n", #condition); return 1; } \
} while (0)

typedef struct {
    rdb_bpp_t links[1];
    uint32_t key;
} consumer_item;

typedef struct {
    rdb_bpp_t links[1];
    __intmax_t key;
} wide_item;

int main(void)
{
    REQUIRE(sizeof(__intmax_t) == RDB_TEST_VALUE_BYTES);
    rdb_init();
    rdb_pool_t *source = rdb_register_um_pool((char *)"consumer-source", 1,
            offsetof(consumer_item, key) - sizeof(rdb_bpp_t), RDB_BTREE | RDB_KUINT32, NULL, 0);
    rdb_pool_t *destination = rdb_register_um_pool((char *)"consumer-destination", 1,
            offsetof(consumer_item, key) - sizeof(rdb_bpp_t), RDB_BTREE | RDB_KUINT32, NULL, 0);
    REQUIRE(source && destination);
    consumer_item *item = (consumer_item *)calloc(1, sizeof(*item));
    REQUIRE(item);
    item->key = 1;
    REQUIRE(rdb_insert(source, item) == 1);
    REQUIRE(rdb_get_const(source, 0, 1) == item);
    __intmax_t missing = ((__intmax_t)1 << 32) + 1;
    REQUIRE(rdb_get_const(source, 0, missing) == NULL);
    REQUIRE(rdb_delete_const(source, 0, missing) == NULL);
    REQUIRE(rdb_move_const(destination, source, 0, missing) == NULL);
    REQUIRE(rdb_move_const(destination, source, 0, 1) == item);
    REQUIRE(rdb_delete_const(destination, 0, 1) == item);
    free(item);
    rdb_drop_pool(source);
    rdb_drop_pool(destination);

    /* Exercise both argument halves on a 128-bit build, and high 64-bit
     * values on the fallback build, without a caller-supplied width flag. */
#if RDB_BUILD_HAS_INT128
    const int wide_flag = RDB_KINT128;
#else
    const int wide_flag = RDB_KINT64;
#endif
    source = rdb_register_um_pool((char *)"wide-source", 1,
            offsetof(wide_item, key) - sizeof(rdb_bpp_t), RDB_BTREE | wide_flag, NULL, 0);
    destination = rdb_register_um_pool((char *)"wide-destination", 1,
            offsetof(wide_item, key) - sizeof(rdb_bpp_t), RDB_BTREE | wide_flag, NULL, 0);
    REQUIRE(source && destination);
    for (int negative = 0; negative < 2; ++negative) {
        wide_item *wide = (wide_item *)calloc(1, sizeof(*wide));
        REQUIRE(wide);
        wide->key = ((__intmax_t)1 << (RDB_TEST_VALUE_BYTES * 8 - 2)) + 37;
        if (negative) wide->key = -wide->key;
        REQUIRE(rdb_insert(source, wide) == 1);
        REQUIRE(rdb_get_const(source, 0, wide->key) == wide);
        REQUIRE(rdb_get_const(source, 0, wide->key + 1) == NULL);
        REQUIRE(rdb_move_const(destination, source, 0, wide->key) == wide);
        REQUIRE(rdb_delete_const(destination, 0, wide->key) == wide);
        free(wide);
    }
    rdb_drop_pool(source);
    rdb_drop_pool(destination);
    rdb_clean(0);
    return 0;
}
