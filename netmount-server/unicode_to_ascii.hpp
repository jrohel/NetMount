// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025-2026 Jaroslav Rohel, jaroslav.rohel@gmail.com

#ifndef _UNICODE_TO_ASCII_HPP_
#define _UNICODE_TO_ASCII_HPP_

#include <filesystem>
#include <string>

void load_transliteration_map(const std::filesystem::path & filepath);

#ifndef _WIN32

// Convert UTF-8 string to ASCII
std::string convert_utf8_to_ascii(const std::string & input);

#else

// Convert Windows UTF-16 string to ASCII
std::string convert_windows_unicode_to_ascii(const std::wstring & input);

#endif

#endif
