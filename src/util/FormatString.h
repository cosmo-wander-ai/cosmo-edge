#pragma once

#include <cstddef>
#include <cstdio>
#include <tuple>
#include <utility>

#include "fmt/format.h"

namespace cosmo::util {

// Expand and merge format parameters
template <typename S, typename T, size_t... N>
auto FormatStringExpand(const S& format, const T& t, std::integer_sequence<size_t, N...>) {
    return fmt::vformat(
        format, fmt::make_args_checked<std::decay_t<decltype(std::get<N>(t))>...>(format, std::get<N>(t)...));
}

// Format parameter count minus 1
template <typename S, typename... Args>
auto FormatString0(const S& format, const Args&... args) {
    return FormatStringExpand(format, std::tie(args...), std::make_index_sequence<sizeof...(args) - 1>());
}

// Expand and merge print parameters
template <typename S, typename T, size_t... N>
auto PrintStringExpand(FILE* file, const S& format, const T& t, std::integer_sequence<size_t, N...>) {
    if (!file)
        return;
    return fmt::vprint(
        file, format,
        fmt::make_args_checked<std::decay_t<decltype(std::get<N>(t))>...>(format, std::get<N>(t)...));
}

// Print parameter count minus 1
template <typename S, typename... Args>
auto PrintString0(FILE* file, const S& format, const Args&... args) {
    return PrintStringExpand(file, format, std::tie(args...),
                             std::make_index_sequence<sizeof...(args) - 1>());
}
}  // namespace cosmo::util

#define COSMO_FORMAT0(fmtstr, ...) cosmo::util::FormatString0(FMT_STRING(fmtstr), __VA_ARGS__)
#define COSMO_PRINT0(fmtstr, ...) cosmo::util::PrintString0(stdout, FMT_STRING(fmtstr), __VA_ARGS__)
#define COSMO_FPRINT0(file, fmtstr, ...) cosmo::util::PrintString0(file, FMT_STRING(fmtstr), __VA_ARGS__)

// Format string
#define COSMO_FORMAT(...) COSMO_FORMAT0(__VA_ARGS__, 0)
// Output string to stdout
#define COSMO_PRINT(...) COSMO_PRINT0(__VA_ARGS__, 0)
// Output string to file
#define COSMO_FPRINT(file, ...) COSMO_FPRINT0(file, __VA_ARGS__, 0)
