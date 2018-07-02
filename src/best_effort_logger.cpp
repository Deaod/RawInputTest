#include "best_effort_logger.hpp"

#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <thread>
#include "aligned_alloc.hpp"
#include "bitmanip.hpp"

namespace belog {

static std::array<std::atomic<thread_buffer_t*>, 256> thread_buffer;
static constexpr u64 SHUTDOWN_SENTINEL_VALUE = ~u64(0);
static std::atomic_bool emergency_shutdown_requested = false;

namespace detail {

thread_buffer_t* _buffer_for_thread(u32 tid) {
    return thread_buffer[tid].load(std::memory_order_relaxed);
}

}

static auto& DIGITS =
    "0001020304050607080910111213141516171819"
    "2021222324252627282930313233343536373839"
    "4041424344454647484950515253545556575859"
    "6061626364656667686970717273747576777879"
    "8081828384858687888990919293949596979899";

template<typename stream, typename type>
void log_integral_value(stream& out, integer_attributes attrs, type val) {
    if (attrs.is_hex) {
        const char* base = attrs.is_uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
        char buffer[32];
        int digits = (bitmanip::find_last_set(val) / 4) + 1;

        size_t idx = 0;
        if (attrs.is_left_aligned == false && attrs.padded_length > digits) {
            while (idx < attrs.padded_length - digits) {
                buffer[idx] = char(attrs.padding_codepoint);
                idx += 1;
            }
        }

        do {
            digits -= 1;
            buffer[idx] = base[(val >> (digits * 4)) & 0xF];
            idx += 1;
        } while (digits > 0);

        if (attrs.is_left_aligned) {
            while (idx < attrs.padded_length) {
                buffer[idx] = char(attrs.padding_codepoint);
                idx += 1;
            }
        }

        out.write(buffer, idx);
    } else {
        char buffer[64];
        memset(buffer, int(attrs.padding_codepoint), sizeof(buffer));

        std::make_unsigned_t<type> abs_val;
        if constexpr (std::is_unsigned_v<type>) {
            abs_val = val;
        } else if (val < 0) {
            abs_val = std::make_unsigned_t<type>(~val + 1);
        } else {
            abs_val = val;
        }

        auto ptr = buffer + 32;

        while (abs_val >= 100) {
            size_t idx = (abs_val % 100) * 2;
            *(ptr--) = DIGITS[idx + 1];
            *(ptr--) = DIGITS[idx];
            abs_val /= 100;
        }
        *(ptr--) = DIGITS[abs_val * 2 + 1];
        if (abs_val >= 10) {
            *(ptr--) = DIGITS[abs_val * 2];
        }
        if (val < 0) {
            *(ptr--) = '-';
        } else if (attrs.show_sign) {
            *(ptr--) = '+';
        }

        size_t write_len = std::max(size_t(attrs.padded_length), size_t((buffer + 32) - ptr));
        
        if (attrs.is_left_aligned) {
            out.write(ptr + 1, write_len);
        } else {
            out.write(buffer + 32 - write_len + 1, write_len);
        }
    }
}

template<typename stream, typename type>
void log_float_value(stream& out, float_attributes attrs, type val) {
    attrs;
    size_t idx = 0;
    char format[16];
    format[idx++] = '%';

    if (attrs.sign_handling == FLOAT_SIGN_SHOW_ALWAYS) {
        format[idx++] = '+';
    } else if (attrs.sign_handling == FLOAT_SIGN_PAD_IF_POSITIVE) {
        format[idx++] = ' ';
    }

    if (attrs.always_show_decimal_point) {
        format[idx++] = '#';
    }

    if (attrs.precision != attrs.precision.MASK) {
        format[idx++] = '.';
        if (attrs.precision >= 10) {
            format[idx++] = DIGITS[attrs.precision * 2];
        }
        format[idx++] = DIGITS[attrs.precision * 2 + 1];
    }

    static auto& display_map = "fFeEaAgG";
    format[idx++] = display_map[attrs.display_style * 2 + attrs.is_uppercase];

    format[idx] = 0;
    char buffer[128];
    size_t len = snprintf(buffer, sizeof(buffer), format, val);
    if (len > 0) {
        out.write(buffer, len);
    }
}

size_t log_integer(integer_data* msg) {
    switch (msg->attributes.length_log2) {
#if CHAR_MAX < SHRT_MAX
        case ctu::log2(sizeof(char)):
            if (msg->attributes.is_unsigned) {
                unsigned char val;
                memcpy(&val, msg->msg, sizeof(val));
                log_integral_value(std::cout, msg->attributes, val);
            } else {
                signed char val;
                memcpy(&val, msg->msg, sizeof(val));
                log_integral_value(std::cout, msg->attributes, val);
            }
            break;
#endif

#if SHRT_MAX < INT_MAX
        case ctu::log2(sizeof(short)):
            if (msg->attributes.is_unsigned) {
                unsigned short val;
                memcpy(&val, msg->msg, sizeof(val));
                log_integral_value(std::cout, msg->attributes, val);
            } else {
                signed short val;
                memcpy(&val, msg->msg, sizeof(val));
                log_integral_value(std::cout, msg->attributes, val);
            }
            break;
#endif

#if INT_MAX < LONG_MAX
        case ctu::log2(sizeof(int)):
            if (msg->attributes.is_unsigned) {
                unsigned int val;
                memcpy(&val, msg->msg, sizeof(val));
                log_integral_value(std::cout, msg->attributes, val);
            } else {
                signed int val;
                memcpy(&val, msg->msg, sizeof(val));
                log_integral_value(std::cout, msg->attributes, val);
            }
            break;
#endif

#if LONG_MAX < LLONG_MAX
        case ctu::log2(sizeof(long)):
            if (msg->attributes.is_unsigned) {
                unsigned long val;
                memcpy(&val, msg->msg, sizeof(val));
                log_integral_value(std::cout, msg->attributes, val);
            } else {
                signed long val;
                memcpy(&val, msg->msg, sizeof(val));
                log_integral_value(std::cout, msg->attributes, val);
            }
            break;
#endif

        case ctu::log2(sizeof(long long)):
            if (msg->attributes.is_unsigned) {
                unsigned long long val;
                memcpy(&val, msg->msg, sizeof(val));
                log_integral_value(std::cout, msg->attributes, val);
            } else {
                signed long long val;
                memcpy(&val, msg->msg, sizeof(val));
                log_integral_value(std::cout, msg->attributes, val);
            }
            break;
    }

    msg->~integer_data();
    return sizeof(integer_data);
}

size_t log_float(float_data* msg) {
    switch (msg->attributes.length_log2) {
#if FLT_MANT_DIG < DBL_MANT_DIG
        case ctu::log2(sizeof(float)):
        {
            float val;
            memcpy(&val, msg->msg, sizeof(val));
            log_float_value(std::cout, msg->attributes, val);
            break;
        }
#endif

#if DBL_MANT_DIG < LDBL_MANT_DIG
        case ctu::log2(sizeof(double)):
        {
            double val;
            memcpy(&val, msg->msg, sizeof(val));
            log_float_value(std::cout, msg->attributes, val);
            break;
        }
#endif

        case ctu::log2(sizeof(long double)):
        {
            long double val;
            memcpy(&val, msg->msg, sizeof(val));
            log_float_value(std::cout, msg->attributes, val);
            break;
        }
    }

    msg->~float_data();
    return sizeof(float_data);
}

size_t log_std_string(std_string_data* msg) {
    std::cout << msg->string;
    msg->~std_string_data();
    return sizeof(std_string_data);
}

size_t log_string_literal(string_literal_data* msg) {
    if (msg->length == msg->UNKNOWN_LENGTH) {
        std::cout.write(msg->address, std::strlen(msg->address));
    } else {
        std::cout.write(msg->address, msg->length - 1);
    }
    msg->~string_literal_data();
    return sizeof(string_literal_data);
}

size_t log_segment(void* data) {
    segment_data* s = static_cast<segment_data*>(data);
    return s->log_func(s);
}

void do_logging() {
    threads::current::assign_id();

    f64 tsc_freq_inverse = 1.0 / f64(tsc_frequency());
    auto start_time = tsc();

    bool shutdown_requested = false;

    for (;;) {
        bool all_threads_empty = true;

        u32 max_id = threads::max_assigned_id();
        for (u32 id = 0; id < max_id; id += 1) {
            if (emergency_shutdown_requested.load(std::memory_order_relaxed))
                return;
            
            auto tbuf = thread_buffer[id].load(std::memory_order_relaxed);
            if (tbuf == nullptr)
                continue;

            auto consumed = tbuf->consume([&](void* storage, size_t length) {
                line_start_data* line = static_cast<line_start_data*>(storage);

                if (line->timepoint == SHUTDOWN_SENTINEL_VALUE) {
                    shutdown_requested = true;
                    line->~line_start_data();
                } else {
                    size_t line_length = length - sizeof(line_start_data);
                    //std::cout << "\n[" << id << "] " << ((line->timepoint - start_time) * tsc_freq_inverse) << ": ";
                    printf("\n[%u] %13.6f: ", id, ((line->timepoint - start_time) * tsc_freq_inverse));

                    line->~line_start_data();

                    char* elem = static_cast<char*>(storage) + sizeof(line_start_data);
                    size_t offset = 0;
                    while (offset < line_length) {
                        offset += log_segment(elem + offset);
                    }
                }

                return true;
            });

            if (consumed) {
                all_threads_empty = false;
            }
        }

        enum class back_off_state {
            spin,
            sleep
        };

        static back_off_state state = back_off_state::spin;
        static i32 spin_counter = 0;
        static constexpr i32 spin_counter_max = 2000;

        if (all_threads_empty) {
            if (shutdown_requested)
                break;

            using namespace std::chrono_literals;
            switch (state) {
                case back_off_state::spin:
                    spin_counter += 1;
                    if (spin_counter >= spin_counter_max) {
                        state = back_off_state::sleep;
                    }
                    break;

                case back_off_state::sleep:
                    std::this_thread::sleep_for(100ms);
                    break;
            }
        } else {
            spin_counter = 0;
            state = back_off_state::spin;
        }
    }
}

bool shutdown() {
    auto tbuf = thread_buffer[threads::current::id()].load(std::memory_order_relaxed);

    return tbuf->produce(sizeof(line_start_data), [](void* storage) {
        new(storage) line_start_data(SHUTDOWN_SENTINEL_VALUE);
        return true;
    });
}

void emergency_shutdown() {
    emergency_shutdown_requested = true;
}

bool enable_logging() {
    threads::current::assign_id();
    auto tid = threads::current::id();
    if (thread_buffer[tid].load(std::memory_order_relaxed) == nullptr) {
        void* space = aligned_alloc(thread_buffer_t::align, sizeof(thread_buffer_t));
        if (space == nullptr) {
            return false;
        }
        thread_buffer[tid].store(new(space) thread_buffer_t(), std::memory_order_relaxed);
    }
    return true;
}

} // namespace belog
