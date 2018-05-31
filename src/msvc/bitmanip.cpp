#include "../bitmanip.hpp"
#include <intrin.h>

namespace bitmanip::detail {

int find_last_set_32(u32 val) {
    unsigned long idx;
    auto nonzero = _BitScanReverse(&idx, val);
    if (nonzero) return int(idx);
    return -1;
}

int find_last_set_64(u64 val) {
    unsigned long idx;
    auto nonzero = _BitScanReverse64(&idx, val);
    if (nonzero) return int(idx);
    return -1;
}

} // namespace bitmanip::detail
