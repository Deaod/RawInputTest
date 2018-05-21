#pragma once

// SYNOPSIS

namespace belog {
template<typename type> struct segment;

bool enable_logging();

template<typename... types>
bool log(types&&... msgs);

struct hex;
struct padding;
template<typename msg_type, typename... fmt_types>
typename segment<msg_type&&>::container_type fmt(msg_type&& msg, fmt_types&&... fmt_attrs);

void do_logging();
bool shutdown();

} // namespace belog

#include <cstdlib>
#include <climits>
#include <string>
#include <array>
#include <atomic>

#include "bitfield.hpp"
#include "cpuid.hpp"
#include "compile_time_utilities.hpp"
#include "spsc_ring_buffer.hpp"
#include "threads.hpp"

namespace belog {

#ifndef BELOG_BUFFER_SIZE_LOG2
#define BELOG_BUFFER_SIZE_LOG2 20
#endif

using thread_buffer_t = spsc_ring_buffer<BELOG_BUFFER_SIZE_LOG2>;
extern std::array<std::atomic<thread_buffer_t*>, 256> thread_buffer;

enum class data_type : char {
    STRING_LITERAL,
    STD_STRING,
    INTEGER,
    FLOAT
};

union integer_attributes {
    u32 all_bits;
    bitfield<u32, 0, 1> length_log2;
    bitfield<u32, 2> is_unsigned;
    bitfield<u32, 3> is_hex;
    bitfield<u32, 4> is_uppercase;
    bitfield<u32, 5> is_left_aligned;
    bitfield<u32, 6, 10> padded_length;
    bitfield<u32, 11, 31> padding_codepoint;

    explicit integer_attributes(u32 initval) :
        all_bits(initval) {}
};

union float_attributes {
    u32 all_bits;
    bitfield<u32, 0, 3> length_log2;
    
    explicit float_attributes(u32 initval) :
        all_bits(initval) {}
};

template<typename... types>
constexpr const auto line_size = ctu::size_of<typename segment<types>::container_type...>;

struct line_start_data {
    u64 timepoint;

    explicit line_start_data(u64 timepoint) :
        timepoint(timepoint)
    {}
};

struct string_literal_data {
    data_type type;
    const char* address;
    size_t length;

    template<size_t literal_length>
    explicit string_literal_data(const char (&msg)[literal_length]) :
        string_literal_data(msg, literal_length)
    {}

    explicit string_literal_data(const char* address, size_t length) :
        type(data_type::STRING_LITERAL),
        address(address),
        length(length)
    {}
};

struct std_string_data {
    data_type type;
    std::string string;

    explicit std_string_data(const std::string& string) :
        type(data_type::STD_STRING),
        string(string)
    {}

    explicit std_string_data(std::string&& string) :
        type(data_type::STD_STRING),
        string(std::move(string))
    {}
};

struct integer_data {
    data_type type;
    integer_attributes attributes;
    char msg[sizeof(long long)];

    template<typename ty>
    explicit integer_data(ty msg) :
        type(data_type::INTEGER),
        attributes(0)
    {
        assign(msg);
    }

    template<typename ty>
    integer_data& operator=(ty msg) {
        assign(msg);

        return *this;
    }

    template<typename ty>
    void assign(ty msg) {
        attributes.is_unsigned = std::is_unsigned_v<ty> ? 1u : 0u;
        attributes.length_log2 = static_cast<u32>(ctu::log2_v<size_t, sizeof(msg)>);
        memcpy(this->msg, &msg, sizeof(msg));
    }
};

struct float_data {
    data_type type;
    float_attributes attributes;
    char msg[sizeof(long double)];

    template<typename ty>
    explicit float_data(ty msg) :
        type(data_type::FLOAT),
        attributes(0)
    {
        attributes.length_log2 = static_cast<u32>(ctu::log2_v<size_t, sizeof(msg)>);
        memcpy(this->msg, &msg, sizeof(msg));
    }
};

template<typename...>struct TD;
template<size_t>struct SD;

// This is the general segment template, instantiation of it should always
// result in a static assertion error during compilation.
template<typename type> struct segment {
    static_assert(std::is_same_v<type, void>, "unknown type for logging");
    using container_type = void;
    bool log(type, void*) {
        static_assert(std::is_same_v<type, void>, "unknown type for logging");
        return false;
    }
    static constexpr size_t size = 0;
};

struct hex {
    void operator()(integer_attributes& attrs) {
        attrs.is_hex = true;
    }
};

struct padding {
    u32 width;
    u32 codepoint;
    u32 is_left_aligned;

    explicit padding(u32 width, u32 codepoint = u32(' '), u32 is_left_aligned = u32(false)) 
        : width(width), codepoint(codepoint), is_left_aligned(is_left_aligned) {}

    void operator()(integer_attributes& attrs) {
        attrs.is_left_aligned = is_left_aligned;
        attrs.padded_length = width;
        attrs.padding_codepoint = codepoint;
    }
};

template<typename msg_type, typename... fmt_types>
typename segment<msg_type&&>::container_type fmt(msg_type&& msg, fmt_types&&... fmt_attrs) {
    using container_type = typename segment<msg_type>::container_type;
    container_type container{ static_cast<decltype(msg)>(msg) };

    (fmt_attrs(container.attributes), ...);

    return container;
}

template<typename... types>
bool log(types&&... msgs) {
    constexpr const auto length = line_size<types...>;
    auto tbuf = thread_buffer[threads::current::id()].load(std::memory_order_relaxed);

    return tbuf->produce(
        sizeof(line_start_data) + length,
        [&msgs...](void* storage) {
            u64 timepoint = tsc();

            new(storage) line_start_data(timepoint);
            char* buffer = static_cast<char*>(storage) + sizeof(line_start_data);

            // This unpacks msgs and calls the log() member function of the appropriate specialization of
            // segment for all arguments passed to this function.
            // Use fold expression in order to avoid recursion which would blow compile time sky high.
            return (segment<types&&>{}.log(
                // forward type as accurately as possible, std::forward does not work for string literals
                static_cast<types&&>(msgs),
                // buffer needs to increase as we unpack, but we need the value of buffer from before it
                // was increased for the current element.
                static_cast<void*>((buffer += segment<types&&>::size) - segment<types&&>::size)
            ) && ...);
        }
    );
}

#define BELOG_SEGMENT_FORWARD(fromType, toType)                 \
    template<> struct segment<fromType> : segment<toType> {}

template<size_t length> struct segment<const char(&)[length]> {
    static_assert(length > 0, "invalid string length");

    bool log(const char (&msg)[length], void* storage) {
        new(storage) string_literal_data(msg);
        return true;
    }

    using container_type = string_literal_data;
    static constexpr size_t size = sizeof(container_type);
};

template<> struct segment<const char*> {
    bool log(const char* msg, void* storage) {
        new(storage) string_literal_data(msg, std::strlen(msg) + 1);
        return true;
    }

    using container_type = string_literal_data;
    static constexpr size_t size = sizeof(container_type);
};

BELOG_SEGMENT_FORWARD(const char*&, const char*);
BELOG_SEGMENT_FORWARD(const char*&&, const char*);

template<> struct segment<std::string&&> {
    bool log(std::string&& msg, void* storage) {
        new(storage) std_string_data(std::move(msg));
        return true;
    }

    using container_type = std_string_data;
    static constexpr size_t size = sizeof(container_type);
};

BELOG_SEGMENT_FORWARD(std::string, std::string&&);

template<> struct segment<const std::string&> {
    bool log(const std::string& msg, void* storage) {
        new(storage) std_string_data(msg);
        return true;
    }

    using container_type = std_string_data;
    static constexpr size_t size = sizeof(container_type);
};

BELOG_SEGMENT_FORWARD(std::string&, const std::string&);

template<> struct segment<bool> {
    bool log(bool msg, void* storage) {
        if (msg) {
            segment<decltype("true")>{}.log("true", storage);
        } else {
            segment<decltype("false")>{}.log("false", storage);
        }
        return true;
    }

    using container_type = string_literal_data;
    static constexpr size_t size = sizeof(container_type);
};

BELOG_SEGMENT_FORWARD(bool&, bool);
BELOG_SEGMENT_FORWARD(const bool&, bool);
BELOG_SEGMENT_FORWARD(bool&&, bool);

template<> struct segment<const integer_data&> {
    bool log(const integer_data& msg, void* storage) {
        new(storage) integer_data(msg);
        return true;
    }

    using container_type = integer_data;
    static constexpr size_t size = sizeof(integer_data);
};

BELOG_SEGMENT_FORWARD(integer_data, const integer_data&);
BELOG_SEGMENT_FORWARD(integer_data&, const integer_data&);
BELOG_SEGMENT_FORWARD(integer_data&&, const integer_data&);

template<> struct segment<const float_data&> {
    bool log(const float_data& msg, void* storage) {
        new(storage) float_data(msg);
        return true;
    }

    using container_type = float_data;
    static constexpr size_t size = sizeof(float_data);
};

BELOG_SEGMENT_FORWARD(float_data, const float_data&);
BELOG_SEGMENT_FORWARD(float_data&, const float_data&);
BELOG_SEGMENT_FORWARD(float_data&&, const float_data&);

#define BELOG_LOG_INTEGRAL_VALUE(type)                         \
    template<> struct segment<type> {                          \
        bool log(type msg, void* storage) {                    \
            new(storage) integer_data(msg);                    \
            return true;                                       \
        }                                                      \
        using container_type = integer_data;                   \
        static constexpr size_t size = sizeof(container_type); \
    }

BELOG_LOG_INTEGRAL_VALUE(char);
BELOG_LOG_INTEGRAL_VALUE(signed char);
BELOG_LOG_INTEGRAL_VALUE(unsigned char);
BELOG_LOG_INTEGRAL_VALUE(signed short);
BELOG_LOG_INTEGRAL_VALUE(unsigned short);
BELOG_LOG_INTEGRAL_VALUE(signed int);
BELOG_LOG_INTEGRAL_VALUE(unsigned int);
BELOG_LOG_INTEGRAL_VALUE(signed long);
BELOG_LOG_INTEGRAL_VALUE(unsigned long);
BELOG_LOG_INTEGRAL_VALUE(signed long long);
BELOG_LOG_INTEGRAL_VALUE(unsigned long long);

BELOG_SEGMENT_FORWARD(char&, char);
BELOG_SEGMENT_FORWARD(signed char&, signed char);
BELOG_SEGMENT_FORWARD(unsigned char&, unsigned char);
BELOG_SEGMENT_FORWARD(signed short&, signed short);
BELOG_SEGMENT_FORWARD(unsigned short&, unsigned short);
BELOG_SEGMENT_FORWARD(signed int&, signed int);
BELOG_SEGMENT_FORWARD(unsigned int&, unsigned int);
BELOG_SEGMENT_FORWARD(signed long&, signed long);
BELOG_SEGMENT_FORWARD(unsigned long&, unsigned long);
BELOG_SEGMENT_FORWARD(signed long long&, signed long long);
BELOG_SEGMENT_FORWARD(unsigned long long&, unsigned long long);

BELOG_SEGMENT_FORWARD(const char&, char);
BELOG_SEGMENT_FORWARD(const signed char&, signed char);
BELOG_SEGMENT_FORWARD(const unsigned char&, unsigned char);
BELOG_SEGMENT_FORWARD(const signed short&, signed short);
BELOG_SEGMENT_FORWARD(const unsigned short&, unsigned short);
BELOG_SEGMENT_FORWARD(const signed int&, signed int);
BELOG_SEGMENT_FORWARD(const unsigned int&, unsigned int);
BELOG_SEGMENT_FORWARD(const signed long&, signed long);
BELOG_SEGMENT_FORWARD(const unsigned long&, unsigned long);
BELOG_SEGMENT_FORWARD(const signed long long&, signed long long);
BELOG_SEGMENT_FORWARD(const unsigned long long&, unsigned long long);

BELOG_SEGMENT_FORWARD(char&&, char);
BELOG_SEGMENT_FORWARD(signed char&&, signed char);
BELOG_SEGMENT_FORWARD(unsigned char&&, unsigned char);
BELOG_SEGMENT_FORWARD(signed short&&, signed short);
BELOG_SEGMENT_FORWARD(unsigned short&&, unsigned short);
BELOG_SEGMENT_FORWARD(signed int&&, signed int);
BELOG_SEGMENT_FORWARD(unsigned int&&, unsigned int);
BELOG_SEGMENT_FORWARD(signed long&&, signed long);
BELOG_SEGMENT_FORWARD(unsigned long&&, unsigned long);
BELOG_SEGMENT_FORWARD(signed long long&&, signed long long);
BELOG_SEGMENT_FORWARD(unsigned long long&&, unsigned long long);

#define BELOG_LOG_FLOAT_VALUE(type)                            \
    template<> struct segment<type> {                          \
        bool log(type msg, void* storage) {                    \
            new(storage) float_data(msg);                      \
            return true;                                       \
        }                                                      \
        using container_type = float_data;                     \
        static constexpr size_t size = sizeof(container_type); \
    }

BELOG_LOG_FLOAT_VALUE(float);
BELOG_LOG_FLOAT_VALUE(double);
BELOG_LOG_FLOAT_VALUE(long double);

BELOG_SEGMENT_FORWARD(float&, float);
BELOG_SEGMENT_FORWARD(double&, double);
BELOG_SEGMENT_FORWARD(long double&, long double);

BELOG_SEGMENT_FORWARD(const float&, float);
BELOG_SEGMENT_FORWARD(const double&, double);
BELOG_SEGMENT_FORWARD(const long double&, long double);

BELOG_SEGMENT_FORWARD(float&&, float);
BELOG_SEGMENT_FORWARD(double&&, double);
BELOG_SEGMENT_FORWARD(long double&&, long double);

} // namespace belog
