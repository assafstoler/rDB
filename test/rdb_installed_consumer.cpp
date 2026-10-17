#include "rdb.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>

struct consumer_record {
    rdb_bpp_t link;
    std::uint32_t key;
};

int main()
{
    char pool_name[] = "installed-cpp-consumer";
    int key_offset = static_cast<int>(offsetof(consumer_record, key) - sizeof(rdb_bpp_t));
    rdb_init();
    rdb_pool_t *pool = rdb_register_um_pool(pool_name, 1, key_offset,
            RDB_BTREE | RDB_KUINT32, nullptr, 0);
    if (!pool) return EXIT_FAILURE;
    auto *record = static_cast<consumer_record *>(
            std::calloc(1, sizeof(consumer_record)));
    if (!record) {
        rdb_drop_pool(pool);
        return EXIT_FAILURE;
    }
    record->key = 29;
    bool ok = rdb_insert(pool, record) == 1 &&
              rdb_get_const(pool, 0, 29) == record &&
              rdb_delete_const(pool, 0, 29) == record;
    std::free(record);
    rdb_drop_pool(pool);
    rdb_clean(0);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
