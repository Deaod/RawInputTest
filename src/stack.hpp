#pragma once

#include <atomic>

template<typename T, size_t SIZE>
struct stack {

    static_assert(sizeof(T) <= 4 * sizeof(void*));

    bool pop(T& elem) {
        size_t size;

        do {
            size = _size.load(std::memory_order_relaxed);
            if (size == 0)
                return false;
        } while (flag.test_and_set(std::memory_order_acquire));

        size -= 1;
        _size.store(size, std::memory_order_relaxed);
        elem = _storage[size];

        flag.clear(std::memory_order_release);
        return true;
    }

    bool push(const T& elem) {
        size_t size;

        do {
            size = _size.load(std::memory_order_relaxed);
            if (size == SIZE)
                return false;
        } while (flag.test_and_set(std::memory_order_acquire));

        _storage[size] = elem;
        _size.store(size + 1, std::memory_order_relaxed);

        flag.clear(std::memory_order_release);
        return true;
    }

    bool empty() {
        return _size.load(std::memory_order_relaxed) == 0;
    }

private:
    std::atomic_flag flag;
    std::atomic<size_t> _size = 0;
    T _storage[SIZE];
};
