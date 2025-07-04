// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#ifndef _LOGGER_HPP_
#define _LOGGER_HPP_

#include "utils.hpp"

#include <array>

enum class LogLevel : int { CRITICAL, ERROR, WARNING, NOTICE, INFO, DEBUG, TRACE };

constexpr auto LOG_LEVEL_C_STR =
    std::to_array<const char *>({"CRITICAL", "ERROR", "WARNING", "NOTICE", "INFO", "DEBUG", "TRACE"});

extern LogLevel global_log_level;

template <typename... Args>
void log(LogLevel level, std::format_string<Args...> fmt, Args &&... args) {
    if (level <= global_log_level) {
        std::string formatted_string = ": " + std::vformat(fmt.get(), std::make_format_args(args...));
        std::fputs((LOG_LEVEL_C_STR[static_cast<int>(level)] + formatted_string).c_str(), stderr);
    }
}

#endif
