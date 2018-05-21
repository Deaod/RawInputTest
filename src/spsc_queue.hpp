#pragma once

#include <atomic>
#include <cstddef>
#include <utility>

template<typename _element_type, int queue_size_log2, int align_log2>
class spsc_queue {
    static const auto queue_size = size_t(1) << queue_size_log2;
    static const auto index_mask = queue_size - 1;

public:
    static const auto align = size_t(1) << align_log2;

    // callback should place an instance of _element_type at the address that is passed to it.
    template<typename cbtype>
    bool produce(cbtype&& callback) noexcept(noexcept(callback(static_cast<void*>(nullptr)))) {
        auto consume_pos = _consume_pos.load(std::memory_order_acquire);
        auto produce_pos = _produce_pos.load(std::memory_order_acquire);

        if ((produce_pos - consume_pos) >= queue_size)
            return false;

        bool cbresult;
        if constexpr (noexcept(callback(static_cast<void*>(nullptr)))) {
            cbresult = callback(static_cast<void*>(_buffer + (produce_pos & index_mask) * sizeof(_element_type)));
        } else {
            try {
                cbresult = callback(static_cast<void*>(_buffer + (produce_pos & index_mask) * sizeof(_element_type)));
            } catch (...) {
                throw;
            }
        }

        if (cbresult) {
            _produce_pos.store(produce_pos + 1, std::memory_order_release);
            return true;
        }

        return false;
    } 

    template<typename cbtype>
    bool consume(cbtype&& callback) noexcept(noexcept(callback(static_cast<_element_type*>(nullptr)))) {
        auto consume_pos = _consume_pos.load(std::memory_order_acquire);
        auto produce_pos = _produce_pos.load(std::memory_order_acquire);

        if ((produce_pos - consume_pos) == 0)
            return false;

        _element_type* elem = reinterpret_cast<_element_type*>(_buffer + (consume_pos & index_mask) * sizeof(_element_type));
        if (callback(elem)) {
            elem->~_element_type();
            _consume_pos.store(consume_pos + 1, std::memory_order_release);
            return true;
        }

        return false;
    }

private:
    std::atomic<size_t> _produce_pos = 0;
    char _padding0[align - sizeof(std::atomic<size_t>)];

    std::atomic<size_t> _consume_pos = 0;
    char _padding1[align - sizeof(std::atomic<size_t>)];

    char _buffer[queue_size * sizeof(_element_type)];
};
