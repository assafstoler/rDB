#include "rdb.h"

#if RDB_BUILD_HAS_INT128 && !defined(USE_128_BIT_TYPES)
#error "The public header did not expose the library's 128-bit setting"
#endif

#if !RDB_BUILD_HAS_INT128 && defined(USE_128_BIT_TYPES)
#error "The public header exposed an unavailable 128-bit setting"
#endif

int main()
{
    rdb_fault_info_t fault = {};
    return rdb_pool_get_fault(nullptr, &fault) == -1 ? 0 : 1;
}
