#pragma once
#include "best_effort_logger.hpp"

#define STRINGIFY_INTERNAL(x) #x
#define STRINGIFY(x) STRINGIFY_INTERNAL(x)

#define LOG_ERR(...) ::belog::log("[E] (" __FILE__ ":" STRINGIFY(__LINE__) ") ", __VA_ARGS__)
#define LOG_WARN(...) ::belog::log("[W] (" __FILE__ ":" STRINGIFY(__LINE__) ") ", __VA_ARGS__)
#define LOG_INFO(...) ::belog::log("[I] (" __FILE__ ":" STRINGIFY(__LINE__) ") ", __VA_ARGS__)
#if defined(_DEBUG)
#define LOG_DEBUG(...) ::belog::log("[D] (" __FILE__ ":" STRINGIFY(__LINE__) ") ", __VA_ARGS__)
#else
#define LOG_DEBUG(...)
#endif

#define DEBUG_BREAK DebugBreak()

#define ON_FAIL_EVAL_TRACE(condition, eval, ...) \
do {                                             \
    const auto& _result_ = (condition);          \
    if (!(eval)) {                               \
        if (!LOG_ERR(__VA_ARGS__))               \
            DEBUG_BREAK;                         \
    }                                            \
} while(0)

#define ON_FAIL_TRACE(condition, ...) ON_FAIL_EVAL_TRACE((condition), _result_, __VA_ARGS__)

#define ON_FAIL_EVAL_TRACE_RETURN(condition, eval, retval, ...) \
do {                                                            \
    const auto& _result_ = (condition);                         \
    if (!(eval)) {                                              \
        if (!LOG_ERR(__VA_ARGS__))                              \
            DEBUG_BREAK;                                        \
        return (retval);                                        \
    }                                                           \
} while(0)

#define ON_FAIL_TRACE_RETURN(condition, retval, ...) ON_FAIL_EVAL_TRACE_RETURN((condition), _result_, (retval), __VA_ARGS__)
#define ON_FAIL_TRACE_RETURN_VOID(condition, ...) ON_FAIL_TRACE_RETURN((condition), (void)0, __VA_ARGS__)
#define ON_FAIL_TRACE_RELAY(condition, ...) ON_FAIL_TRACE_RETURN((condition), _result_, __VA_ARGS__)

#define ON_FAIL_RETURN(condition, retval) ON_FAIL_TRACE_RETURN((condition), (retval), "Failed: "#condition)
#define ON_FAIL_RETURN_VOID(condition) ON_FAIL_RETURN((condition), (void)0)
#define ON_FAIL_RELAY(condition) ON_FAIL_RETURN((condition), _result_)

#define ON_FAIL_EVAL_TRACE_THROW(condition, eval, exception, ...) \
do {                                                              \
    const auto& _result_ = (condition);                           \
    if (!(eval)) {                                                \
        if (!LOG_ERR(__VA_ARGS__))                                \
            DEBUG_BREAK;                                          \
        throw (exception);                                        \
    }                                                             \
} while(0)

#define ON_FAIL_TRACE_THROW(condition, exception, ...) ON_FAIL_EVAL_TRACE_THROW((condition), _result_, (exception), __VA_ARGS__)
#define ON_FAIL_THROW(condition, exception) ON_FAIL_TRACE_THROW((condition), (exception), "Failed: "#condition)
