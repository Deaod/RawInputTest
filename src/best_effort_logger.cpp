#include "best_effort_logger.hpp"

#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <thread>
#include "aligned_alloc.hpp"

namespace belog {

std::array<std::atomic<thread_buffer_t*>, 256> thread_buffer;
static constexpr u64 SHUTDOWN_SENTINEL_VALUE = ~u64(0);

template<typename type>
void log_integral_value(integer_attributes attrs, type val) {
    std::stringstream out;
    out << (attrs.is_hex ? std::hex : std::dec)
        << (attrs.is_uppercase ? std::uppercase : std::nouppercase)
        << (attrs.is_left_aligned ? std::left : std::right)
        << std::setw(attrs.padded_length)
        << std::setfill(static_cast<char>(attrs.padding_codepoint))
        << val;
    std::cout << out.str();
}

template<typename type>
void log_float_value(float_attributes attrs, type val) {
    attrs;
    std::stringstream out;
    out << val;
    std::cout << out.str();
}

void log_integer(const integer_data& msg) {
    switch (msg.attributes.length_log2) {
#if CHAR_MAX < SHRT_MAX
        case ctu::log2(sizeof(char)):
            if (msg.attributes.is_unsigned) {
                unsigned char val;
                memcpy(&val, msg.msg, sizeof(val));
                log_integral_value(msg.attributes, val);
            } else {
                signed char val;
                memcpy(&val, msg.msg, sizeof(val));
                log_integral_value(msg.attributes, val);
            }
            break;
#endif

#if SHRT_MAX < INT_MAX
        case ctu::log2(sizeof(short)):
            if (msg.attributes.is_unsigned) {
                unsigned short val;
                memcpy(&val, msg.msg, sizeof(val));
                log_integral_value(msg.attributes, val);
            } else {
                signed short val;
                memcpy(&val, msg.msg, sizeof(val));
                log_integral_value(msg.attributes, val);
            }
            break;
#endif

#if INT_MAX < LONG_MAX
        case ctu::log2(sizeof(int)):
            if (msg.attributes.is_unsigned) {
                unsigned int val;
                memcpy(&val, msg.msg, sizeof(val));
                log_integral_value(msg.attributes, val);
            } else {
                signed int val;
                memcpy(&val, msg.msg, sizeof(val));
                log_integral_value(msg.attributes, val);
            }
            break;
#endif

#if LONG_MAX < LLONG_MAX
        case ctu::log2(sizeof(long)):
            if (msg.attributes.is_unsigned) {
                unsigned long val;
                memcpy(&val, msg.msg, sizeof(val));
                log_integral_value(msg.attributes, val);
            } else {
                signed long val;
                memcpy(&val, msg.msg, sizeof(val));
                log_integral_value(msg.attributes, val);
            }
            break;
#endif

        case ctu::log2(sizeof(long long)):
            if (msg.attributes.is_unsigned) {
                unsigned long long val;
                memcpy(&val, msg.msg, sizeof(val));
                log_integral_value(msg.attributes, val);
            } else {
                signed long long val;
                memcpy(&val, msg.msg, sizeof(val));
                log_integral_value(msg.attributes, val);
            }
            break;
    }
}

void log_float(const float_data& msg) {
    switch (msg.attributes.length_log2) {
#if FLT_MANT_DIG < DBL_MANT_DIG
        case ctu::log2(sizeof(float)):
        {
            float val;
            memcpy(&val, msg.msg, sizeof(val));
            log_float_value(msg.attributes, val);
            break;
        }
#endif

#if DBL_MANT_DIG < LDBL_MANT_DIG
        case ctu::log2(sizeof(double)):
        {
            double val;
            memcpy(&val, msg.msg, sizeof(val));
            log_float_value(msg.attributes, val);
            break;
        }
#endif

        case ctu::log2(sizeof(long double)):
        {
            long double val;
            memcpy(&val, msg.msg, sizeof(val));
            log_float_value(msg.attributes, val);
            break;
        }
    }
}

size_t log_segment(void* data) {
    union log_data {
        data_type type;
        string_literal_data string_literal;
        std_string_data std_string;
        integer_data integer;
        float_data float_;

        // avoid warnings about implicitly deleted destructor by explicitly deleting it
        ~log_data() = delete;
    };

    log_data* ld = reinterpret_cast<log_data*>(data);

    switch (ld->type) {
        case data_type::STRING_LITERAL: {
            std::cout.write(ld->string_literal.address, ld->string_literal.length - 1);
            ld->string_literal.~string_literal_data();
            return sizeof(string_literal_data);
        }

        case data_type::STD_STRING: {
            std::cout << ld->std_string.string;
            ld->std_string.~std_string_data();
            return sizeof(std_string_data);
        }

        case data_type::INTEGER: {
            log_integer(ld->integer);
            ld->integer.~integer_data();
            return sizeof(integer_data);
        }

        case data_type::FLOAT: {
            log_float(ld->float_);
            ld->float_.~float_data();
            return sizeof(float_data);
        }

        default:
            break;
    }
    return 0;
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
            auto tbuf = thread_buffer[id].load(std::memory_order_relaxed);
            if (tbuf == nullptr)
                continue;

            auto consumed = tbuf->consume([&](void* storage, size_t length) {
                line_start_data* line = reinterpret_cast<line_start_data*>(storage);

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
