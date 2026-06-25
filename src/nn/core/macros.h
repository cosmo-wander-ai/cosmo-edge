#pragma once

#include <stdio.h>
#include <stdlib.h>

// Interface visibility
#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef BUILDING_DLL
#ifdef __GNUC__
#define PUBLIC __attribute__((dllexport))
#else  // __GNUC__
#define PUBLIC __declspec(dllexport)
#endif  // __GNUC__
#else   // BUILDING_DLL
#ifdef __GNUC__
#define PUBLIC __attribute__((dllimport))
#else
#define PUBLIC __declspec(dllimport)
#endif  // __GNUC__
#endif  // BUILDING_DLL
#define LOCAL
#else  // _WIN32 || __CYGWIN__
#if __GNUC__ >= 4
#define PUBLIC __attribute__((visibility("default")))
#define LOCAL __attribute__((visibility("hidden")))
#else
#define PUBLIC
#define LOCAL
#endif
#endif

// deprecated
#ifndef DEPRECATED
#if defined(__GNUC__) || defined(__clang__)
#define DEPRECATED(msg) __attribute__((deprecated(msg)))
#elif defined(_MSC_VER)
#define DEPRECATED(msg) __declspec(deprecated(msg))
#else
#pragma message("WARNING: You need to implement DEPRECATED for this compiler")
#define DEPRECATED
#endif
#endif

// log
#ifdef __ANDROID__
#include <android/log.h>
#define LOGDT(fmt, tag, ...)                                                                                 \
    __android_log_print(ANDROID_LOG_DEBUG, tag, ("%s [File %s][Line %d] " fmt), __PRETTY_FUNCTION__,         \
                        __FILE__, __LINE__, ##__VA_ARGS__)

#define LOGIT(fmt, tag, ...)                                                                                 \
    __android_log_print(ANDROID_LOG_INFO, tag, ("%s [File %s][Line %d] " fmt), __PRETTY_FUNCTION__,          \
                        __FILE__, __LINE__, ##__VA_ARGS__)

#define LOGET(fmt, tag, ...)                                                                                 \
    __android_log_print(ANDROID_LOG_ERROR, tag, ("%s [File %s][Line %d] " fmt), __PRETTY_FUNCTION__,         \
                        __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define LOGDT(fmt, tag, ...)                                                                                 \
    fprintf(stdout, ("D/%s: %s [File %s][Line %d] " fmt), tag, __FUNCTION__, __FILE__, __LINE__,             \
            ##__VA_ARGS__)

#define LOGIT(fmt, tag, ...)                                                                                 \
    fprintf(stdout, ("I/%s: %s [File %s][Line %d] " fmt), tag, __FUNCTION__, __FILE__, __LINE__,             \
            ##__VA_ARGS__)

#define LOGET(fmt, tag, ...)                                                                                 \
    fprintf(stderr, ("E/%s: %s [File %s][Line %d] " fmt), tag, __FUNCTION__, __FILE__, __LINE__,             \
            ##__VA_ARGS__)

#endif

#define DEFAULT_TAG "cosmo.nn"
#define LOGD(fmt, ...) LOGDT(fmt, DEFAULT_TAG, ##__VA_ARGS__)
#define LOGI(fmt, ...) LOGIT(fmt, DEFAULT_TAG, ##__VA_ARGS__)
#define LOGE(fmt, ...) LOGET(fmt, DEFAULT_TAG, ##__VA_ARGS__)
#define LOGE_IF(cond, fmt, ...)                                                                              \
    if (cond) {                                                                                              \
        LOGET(fmt, DEFAULT_TAG, ##__VA_ARGS__);                                                              \
    }

// assert
#include <cassert>
#define ASSERT(x)                                                                                            \
    {                                                                                                        \
        int res = (x);                                                                                       \
        if (!res) {                                                                                          \
            LOGE("Error: assert failed.\n");                                                                 \
            assert(x);                                                                                       \
        }                                                                                                    \
    }

#ifndef DEBUG
#undef LOGDT
#undef LOGD
#define LOGDT(tag, fmt, ...)
#define LOGD(fmt, ...)
#undef ASSERT
#define ASSERT(x)
#endif

// break if
#define BREAK_IF(cond)                                                                                       \
    if (cond)                                                                                                \
    break

#ifdef __OPTIMIZE__
#define BREAK_IF_MSG(cond, msg)                                                                              \
    if (cond)                                                                                                \
    break
#else
#define BREAK_IF_MSG(cond, msg)                                                                              \
    if (cond)                                                                                                \
        LOGD(msg);                                                                                           \
    break
#endif

// SAFE_DELETE removed

#include <cfloat>
#include <cstdint>

namespace cosmo::nn {

template <typename T>
constexpr T UpDiv(T x, T y) {
    return (x + y - 1) / y;
}

template <typename T>
constexpr T RoundUp(T x, T y) {
    return ((x + y - 1) / y) * y;
}

template <typename T>
constexpr T AlignUp4(T x) {
    return RoundUp(x, T(4));
}

template <typename T>
constexpr T AlignUp8(T x) {
    return RoundUp(x, T(8));
}

}  // namespace cosmo::nn

#define RETURN_ON_NEQ(status, expected)                                                                      \
    do {                                                                                                     \
        auto s = (status);                                                                                   \
        if (s != (expected)) {                                                                               \
            return s;                                                                                        \
        }                                                                                                    \
    } while (0)

#define RETURN_VALUE_ON_NEQ(status, expected, value)                                                         \
    do {                                                                                                     \
        auto s = (status);                                                                                   \
        if (s != (expected)) {                                                                               \
            return (value);                                                                                  \
        }                                                                                                    \
    } while (0)

#define RETURN_ON_FAIL(status) RETURN_ON_NEQ(status, cosmo::nn::COSMO_NN_OK)

#define CHECK_PARAM_NULL(param)                                                                              \
    do {                                                                                                     \
        if (!param) {                                                                                        \
            LOGE("Error: param is nullptr\n");                                                               \
            return Status(COSMO_NN_ERR_NULL_PARAM, "Error: param is nullptr");                               \
        }                                                                                                    \
    } while (0)

#define GET_OFFSET_PTR(ptr, offset) (reinterpret_cast<int8_t*>(ptr) + offset)
