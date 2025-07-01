// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#ifndef _UTILS_HPP_
#define _UTILS_HPP_

#include <bit>
#include <cstdint>
#include <cstdio>
#include <format>

//#define DEBUG

// Enables packet loss simulation (for tests)
//#define SIMULATE_PACKET_LOSS

// The C++23 language provides `std::print`.
// We define a `print` macro with a similar function for C++20.
#define print(stream, fmt, ...)                                                  \
    {                                                                            \
        std::fputs(std::format(fmt __VA_OPT__(, ) __VA_ARGS__).c_str(), stream); \
    }

#define err_print(fmt, ...)                                                      \
    {                                                                            \
        std::fputs(std::format(fmt __VA_OPT__(, ) __VA_ARGS__).c_str(), stderr); \
    }

#ifdef DEBUG
#define dbg_print(fmt, ...)                                                      \
    {                                                                            \
        std::fputs(std::format(fmt __VA_OPT__(, ) __VA_ARGS__).c_str(), stderr); \
    }
#else
#define dbg_print(fmt, ...)
#endif


inline char ascii_to_upper(char c) {
    if ((c >= 'a') && (c <= 'z')) {
        c -= 'a' - 'A';
    }
    return c;
}


inline char ascii_to_lower(char c) {
    if (c >= 'A' && c <= 'Z') {
        c += 'a' - 'A';
    }
    return c;
}


inline std::uint16_t byte_swap16(std::uint16_t x) { return (x >> 8) | (x << 8); }


inline std::uint32_t byte_swap32(std::uint32_t x) {
    return (x >> 24) | ((x & 0x00FF0000U) >> 8) | ((x & 0x0000FF00U) << 8) | (x << 24);
}


inline std::uint16_t to_little16(std::uint16_t host_value) {
    if constexpr (std::endian::native == std::endian::little) {
        return host_value;
    }
    return byte_swap16(host_value);
}


inline std::uint32_t to_little32(std::uint32_t host_value) {
    if constexpr (std::endian::native == std::endian::little) {
        return host_value;
    }
    return byte_swap32(host_value);
}


inline std::uint16_t from_little16(std::uint16_t little_value) {
    if constexpr (std::endian::native == std::endian::little) {
        return little_value;
    }
    return byte_swap16(little_value);
}


inline std::uint32_t from_little32(std::uint32_t little_value) {
    if constexpr (std::endian::native == std::endian::little) {
        return little_value;
    }
    return byte_swap32(little_value);
}


inline std::uint16_t to_big16(std::uint16_t host_value) {
    if constexpr (std::endian::native == std::endian::big) {
        return host_value;
    }
    return byte_swap16(host_value);
}


inline std::uint32_t to_big32(std::uint32_t host_value) {
    if constexpr (std::endian::native == std::endian::big) {
        return host_value;
    }
    return byte_swap32(host_value);
}


inline std::uint16_t from_big16(std::uint16_t little_value) {
    if constexpr (std::endian::native == std::endian::big) {
        return little_value;
    }
    return byte_swap16(little_value);
}


inline std::uint32_t from_big32(std::uint32_t little_value) {
    if constexpr (std::endian::native == std::endian::big) {
        return little_value;
    }
    return byte_swap32(little_value);
}

#endif
