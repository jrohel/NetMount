// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#include "unicode_to_ascii.hpp"

#include "logger.hpp"

#include <cstdint>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace {

// Unicode to ASCII transliteration map
std::unordered_map<std::uint32_t, std::string> transliteration_map;


// Trim leading/trailing spaces and optional quotes
void clean_token(std::string_view & token) {
    // Trim whitespace
    auto start = token.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) {
        token = {};
        return;
    }
    auto end = token.find_last_not_of(" \t\r\n");

    // Remove surrounding quotes if present
    if (end - start >= 1 && token[start] == '"' && token[end] == '"') {
        ++start;
        --end;
    }

    token = token.substr(start, end - start + 1);
}


// Convert single UTF-8 character to Unicode codepoint
std::pair<std::uint32_t, bool> utf8_to_codepoint(std::string_view utf8_char) {
    const auto * const bytes = reinterpret_cast<const unsigned char *>(utf8_char.data());
    const auto len = utf8_char.size();

    if (len == 1) {
        return {bytes[0], true};
    }
    if ((bytes[0] & 0xE0) == 0xC0 && len >= 2) {
        return {((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F), true};
    }
    if ((bytes[0] & 0xF0) == 0xE0 && len >= 3) {
        return {((bytes[0] & 0x0F) << 12) | ((bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F), true};
    }
    if ((bytes[0] & 0xF8) == 0xF0 && len >= 4) {
        return {
            ((bytes[0] & 0x07) << 18) | ((bytes[1] & 0x3F) << 12) | ((bytes[2] & 0x3F) << 6) | (bytes[3] & 0x3F), true};
    }

    return {0xFFFD, false};  // Replacement char
}


bool is_combining_mark(std::uint32_t cp) {
    // This covers the most-used combining ranges.
    return (cp >= 0x0300 && cp <= 0x036F) ||  // Combining Diacritical Marks
           (cp >= 0x1AB0 && cp <= 0x1AFF) ||  // Combining Diacritical Marks Extended
           (cp >= 0x1DC0 && cp <= 0x1DFF) ||  // Combining Diacritical Marks Supplement
           (cp >= 0x20D0 && cp <= 0x20FF) ||  // Combining Diacritical Marks for Symbols
           (cp >= 0xFE20 && cp <= 0xFE2F);    // Combining Half Marks
}

}  // namespace


void load_transliteration_map(const std::filesystem::path & filename) {
    std::ifstream file(filename);
    if (!file) {
        auto message = std::system_category().message(errno);
        throw std::runtime_error(
            std::format("Unable to open transliteration map file \"{}\": {}", filename.string(), message));
    }

    std::string line;
    size_t line_number = 0;

    while (std::getline(file, line)) {
        ++line_number;

        if (line.empty() || line[0] == '#') {
            continue;
        }

        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            log(LogLevel::WARNING, "Missing ':' in file \"{}\" on line {}\n", filename.string(), line_number);
        }

        auto key = std::string_view(line.begin(), line.begin() + colon);
        auto value = std::string_view(line.begin() + colon + 1, line.end());
        clean_token(key);
        clean_token(value);

        if (key.empty()) {
            log(LogLevel::WARNING, "Empty key in file \"{}\" on line {}\n", filename.string(), line_number);
        }

        const auto [cp, is_ok] = utf8_to_codepoint(key);
        if (!is_ok) {
            log(LogLevel::WARNING, "Invalid UTF-8 key in file \"{}\" on line {}\n", filename.string(), line_number);
        }

        const auto [it, inserted] = transliteration_map.try_emplace(cp, value);
        if (!inserted && value != it->second) {
            log(LogLevel::WARNING,
                "The key '{}' in file \"{}\" on line {} has already been inserted with a different value\n",
                key,
                filename.string(),
                line_number);
        }
    }
}


// Convert UTF-8 string to ASCII
std::string convert_utf8_to_ascii(const std::string & input) {
    std::string result;

    for (size_t i = 0; i < input.size();) {
        const unsigned char c = input[i];
        size_t len = 1;
        if ((c & 0x80) == 0x00) {
            result += c;
        } else {
            if ((c & 0xE0) == 0xC0) {
                len = 2;
            } else if ((c & 0xF0) == 0xE0) {
                len = 3;
            } else if ((c & 0xF8) == 0xF0) {
                len = 4;
            } else {
                result += '_';
                ++i;
                continue;
            }

            if (i + len > input.size()) {
                break;
            }

            auto utf8_char = std::string_view(input.begin() + i, input.begin() + i + len);
            const auto [cp, is_ok] = utf8_to_codepoint(utf8_char);

            if (!is_combining_mark(cp)) {
                auto it = transliteration_map.find(cp);
                if (it != transliteration_map.end()) {
                    result += it->second;
                } else {
                    result += '_';
                }
            }
        }
        i += len;
    }

    return result;
}


// Convert Windows UTF-16 string to ASCII
std::string convert_windows_unicode_to_ascii(const std::wstring & input) {
    std::string result;

    for (size_t i = 0; i < input.size();) {
        const wchar_t wc = input[i];

        // Handle surrogate pair
        if (wc >= 0xD800 && wc <= 0xDBFF && (i + 1) < input.size()) {
            const wchar_t wc2 = input[i + 1];
            if (wc2 >= 0xDC00 && wc2 <= 0xDFFF) {
                const std::uint32_t cp = (((wc - 0xD800) << 10) | (wc2 - 0xDC00)) + 0x10000;
                if (!is_combining_mark(cp)) {
                    auto it = transliteration_map.find(cp);
                    if (it != transliteration_map.end()) {
                        result += it->second;
                    } else {
                        result += '_';
                    }
                }
                i += 2;
                continue;
            }
        }

        const auto cp = static_cast<std::uint32_t>(wc);
        if (cp <= 0x7F) {
            result += static_cast<char>(cp);
        } else if (!is_combining_mark(cp)) {
            auto it = transliteration_map.find(cp);
            if (it != transliteration_map.end()) {
                result += it->second;
            } else {
                result += '_';
            }
        }
        ++i;
    }

    return result;
}
