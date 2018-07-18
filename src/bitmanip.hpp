#pragma once
#include "types.hpp"

namespace bitmanip {

namespace detail {
int find_last_set_64(u64 val);
int find_last_set_32(u32 val);
}

template<typename type>
int find_last_set(type val) {
    if constexpr(sizeof(type) > 4) {
        return detail::find_last_set_64(u64(val));
    } else {
        return detail::find_last_set_32(u32(val));
    }
}

}//namespace bitmanip
