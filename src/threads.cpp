#include "threads.hpp"

#include <atomic>
#include "stack.hpp"

namespace threads {
std::atomic<u32> thread_counter = 1;
stack<u32, 128> released_ids;

namespace current {
thread_local u32 thread_id = 0;

void assign_id() {
    if (thread_id == 0) {
        if (released_ids.pop(thread_id) == false) {
            thread_id = thread_counter.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void release_id() {
    if (thread_id == 0)
        return;

    if (released_ids.push(thread_id)) {
        thread_id = 0;
    }
}

u32 id() {
    return thread_id;
}

} // namespace current

u32 max_assigned_id() {
    return thread_counter.load(std::memory_order_relaxed);
}

} // namespace threads