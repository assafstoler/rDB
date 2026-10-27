/* No prerequisite includes: also expand logging macros and test the guard. */
#include "rdb.h"
#include "rdb.h"

#if RDB_BUILD_HAS_INT128 && !defined(USE_128_BIT_TYPES)
#error "The public header did not expose the library's 128-bit setting"
#endif

#if !RDB_BUILD_HAS_INT128 && defined(USE_128_BIT_TYPES)
#error "The public header exposed an unavailable 128-bit setting"
#endif

enum { DEBUG_FLAGS = 1 };

int main(void)
{
    rdb_fault_info_t fault;
    if (rdb_pool_get_fault(NULL, &fault) != -1) return 1;
    rdb_key_union key = {0};
    key.sst = -1;
    if (key.sst >= 0) return 1;

    rdb_c_info("%s", "header logging check\n");
    rdb_info("%s", "header info check\n");
    rdb_l_info(1, "%s", "header flag check\n");
    debug("%s", "header debug check\n");

    rdb_init();
    char name[] = "header-consumer";
    rdb_pool_t *pool = rdb_register_um_pool(name, 1, 0, RDB_BTREE | RDB_KINT32, NULL, 0);
    if (!pool) rdb_fatal("%s", "header consumer pool registration failed\n");
    if (rdb_lock(pool, __func__) != 0) return 1;
    rdb_unlock(pool, __func__);
    rdb_clean(0);
    return 0;
}
