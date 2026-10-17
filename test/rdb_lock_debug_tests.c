#include <stddef.h>
#include <stdint.h>
#include "rdb.h"

typedef struct {
    rdb_bpp_t link;
    int32_t key;
} record_t;

int main(void)
{
    int key_offset = (int)(offsetof(record_t, key) - sizeof(rdb_bpp_t));
    rdb_pool_t *pool;

    rdb_init();
    pool = rdb_register_um_pool("lock-debug", 1, key_offset,
            RDB_BTREE | RDB_KINT32, NULL, 0);
    if (!pool)
        return 1;
    if (rdb_lock(pool, "debug-test") != 0) {
        rdb_clean(0);
        return 1;
    }
    rdb_unlock(pool, "debug-test");
    rdb_drop_pool(pool);
    return 0;
}
