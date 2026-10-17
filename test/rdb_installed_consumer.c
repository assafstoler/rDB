#include "rdb.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    rdb_bpp_t link;
    uint32_t key;
} consumer_record_t;

int main(void)
{
    char pool_name[] = "installed-c-consumer";
    int key_offset = (int)(offsetof(consumer_record_t, key) - sizeof(rdb_bpp_t));
    rdb_pool_t *pool;
    consumer_record_t *record;

    rdb_init();
    pool = rdb_register_um_pool(pool_name, 1, key_offset,
            RDB_BTREE | RDB_KUINT32, NULL, 0);
    if (!pool) return EXIT_FAILURE;
    record = calloc(1, sizeof(*record));
    if (!record) {
        rdb_drop_pool(pool);
        return EXIT_FAILURE;
    }
    record->key = 17;
    if (rdb_insert(pool, record) != 1 || rdb_get_const(pool, 0, 17) != record ||
            rdb_delete_const(pool, 0, 17) != record) {
        rdb_drop_pool(pool);
        free(record);
        return EXIT_FAILURE;
    }
    free(record);
    rdb_drop_pool(pool);
    rdb_clean(0);
    return EXIT_SUCCESS;
}
