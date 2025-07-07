// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#ifndef _LOGGER_HPP_
#define _LOGGER_HPP_

#include <format>

enum class LogLevel : int { CRITICAL, ERROR, WARNING, NOTICE, INFO, DEBUG, TRACE };

extern LogLevel global_log_level;

namespace detail {

void log(LogLevel level, const std::string & message);

}


template <typename... Args>
void log(LogLevel level, std::format_string<Args...> fmt, Args &&... args) {
    if (level <= global_log_level) {
        auto message = std::vformat(fmt.get(), std::make_format_args(args...));
        detail::log(level, message);
    }
}

#endif
