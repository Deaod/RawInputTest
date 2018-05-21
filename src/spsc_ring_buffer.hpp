#pragma once

#include <atomic>
#include <cstddef>
#include "compile_time_utilities.hpp"

template<int buffer_size_log2, int align_log2 = 6>
struct spsc_ring_buffer {
    static const auto size = size_t(1) << buffer_size_log2;
    static const auto mask = size - 1;
    static const auto align = size_t(1) << align_log2;

    template<typename cbtype>
    bool produce(size_t length, cbtype callback) noexcept(noexcept(callback(static_cast<void*>(nullptr)))) {
        if (length <= 0 || length >= size)
            return false;

        auto consume_pos = _consume_pos.load(std::memory_order_acquire);
        auto produce_pos = _produce_pos.load(std::memory_order_acquire);

        auto rounded_length = ctu::round_up_bits(length, ctu::log2_v<size_t, sizeof(ptrdiff_t)>);

        if ((produce_pos - consume_pos) > (size - (rounded_length + sizeof(ptrdiff_t))))
            return false;

        auto wrap_distance = size - (produce_pos & mask);
        if (wrap_distance < (rounded_length + sizeof(ptrdiff_t))) {
            new (_buffer + (produce_pos & mask)) ptrdiff_t(-ptrdiff_t(wrap_distance));
            produce_pos += wrap_distance;

            if ((produce_pos - consume_pos) > (size - (rounded_length + sizeof(ptrdiff_t))))
                return false;
        }

        new (_buffer + (produce_pos & mask)) ptrdiff_t(length);
        if (callback(static_cast<void*>(_buffer + (produce_pos & mask) + sizeof(ptrdiff_t)))) {
            _produce_pos.store(produce_pos + (rounded_length + sizeof(ptrdiff_t)), std::memory_order_release);
            return true;
        }

        return false;
    }

    template<typename cbtype>
    bool consume(cbtype callback) noexcept(noexcept(callback(static_cast<void*>(nullptr), ptrdiff_t(0)))) {
        auto consume_pos = _consume_pos.load(std::memory_order_acquire);
        auto produce_pos = _produce_pos.load(std::memory_order_acquire);

        if (produce_pos == consume_pos)
            return false;

        ptrdiff_t length;
        memcpy(&length, _buffer + (consume_pos & mask), sizeof(length));
        
        if (length < 0) {
            consume_pos += -length;
            memcpy(&length, _buffer + (consume_pos & mask), sizeof(length));
        }

        if (callback(static_cast<void*>(_buffer + (consume_pos & mask) + sizeof(ptrdiff_t)), length)) {
            auto rounded_length = ctu::round_up_bits(length, ctu::log2_v<size_t, sizeof(ptrdiff_t)>);
            _consume_pos.store(consume_pos + (rounded_length + sizeof(ptrdiff_t)), std::memory_order_release);
            return true;
        }

        return false;
    }

    // returns true if buffer is empty after this call
    template<typename cbtype>
    bool consume_all(cbtype callback) noexcept(noexcept(callback(static_cast<void*>(nullptr), ptrdiff_t(0)))) {
        auto consume_pos = _consume_pos.load(std::memory_order_acquire);
        auto produce_pos = _produce_pos.load(std::memory_order_acquire);

        if (produce_pos == consume_pos)
            return true;

        while (consume_pos != produce_pos) {
            while (consume_pos != produce_pos) {
                ptrdiff_t length;
                memcpy(&length, _buffer + (consume_pos & mask), sizeof(length));

                if (length < 0) {
                    consume_pos += -length;
                    memcpy(&length, _buffer + (consume_pos & mask), sizeof(length));
                }

                if (callback(static_cast<void*>(_buffer + (consume_pos & mask) + sizeof(ptrdiff_t)), length) == false) {
                    _consume_pos.store(consume_pos, std::memory_order_release);
                    return false;
                }

                auto rounded_length = ctu::round_up_bits(length, ctu::log2_v<size_t, sizeof(ptrdiff_t)>);
                consume_pos += (rounded_length + sizeof(ptrdiff_t));
            }

            produce_pos = _produce_pos.load(std::memory_order_acquire);
        }

        _consume_pos.store(consume_pos, std::memory_order_release);
        return (consume_pos == produce_pos);
    }

    bool is_empty() const noexcept {
        auto produce_pos = _produce_pos.load(std::memory_order_relaxed);
        auto consume_pos = _consume_pos.load(std::memory_order_relaxed);

        return produce_pos == consume_pos;
    }

private:
    std::atomic<size_t> _produce_pos = 0;
    char _padding0[align - sizeof(std::atomic<size_t>)];

    std::atomic<size_t> _consume_pos = 0;
    char _padding1[align - sizeof(std::atomic<size_t>)];

    char _buffer[size];
};