// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#include "fs.hpp"
#include "logger.hpp"


#if DOS_ATTRS_NATIVE == 1

#include <windows.h>

// Undefine macro 'ERROR' from Windows headers to prevent conflicts with our enum
#undef ERROR

namespace {

std::string get_error_message(int error_code) {
    char * msg_buffer = nullptr;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&msg_buffer,
        0,
        nullptr);
    std::unique_ptr<char, decltype(&LocalFree)> msg_buffer_owner(msg_buffer, LocalFree);
    const std::string message = msg_buffer ? msg_buffer : "Unknown error";
    return message;
}

}  // namespace


namespace netmount_srv {

bool is_dos_attrs_native_supported([[maybe_unused]] const std::filesystem::path & path) { return true; }


uint8_t get_dos_attrs_native(const std::filesystem::path & path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        throw std::runtime_error(
            std::format(
                "get_dos_attrs_native: Failed to fetch attributes of \"{}\": {}\n",
                path.string(),
                get_error_message(GetLastError())));
    }
    uint8_t ret = 0;
    if (attrs & FILE_ATTRIBUTE_READONLY) {
        ret |= FAT_RO;
    }
    if (attrs & FILE_ATTRIBUTE_HIDDEN) {
        ret |= FAT_HIDDEN;
    }
    if (attrs & FILE_ATTRIBUTE_SYSTEM) {
        ret |= FAT_SYSTEM;
    }
    if (attrs & FILE_ATTRIBUTE_ARCHIVE) {
        ret |= FAT_ARCHIVE;
    }
    return ret;
}


void set_dos_attrs_native(const std::filesystem::path & path, uint8_t attrs) {
    DWORD win_attrs = 0;
    if (attrs & FAT_RO) {
        win_attrs |= FILE_ATTRIBUTE_READONLY;
    }
    if (attrs & FAT_HIDDEN) {
        win_attrs |= FILE_ATTRIBUTE_HIDDEN;
    }
    if (attrs & FAT_SYSTEM) {
        win_attrs |= FILE_ATTRIBUTE_SYSTEM;
    }
    if (attrs & FAT_ARCHIVE) {
        win_attrs |= FILE_ATTRIBUTE_ARCHIVE;
    }
    const auto result = SetFileAttributesW(path.c_str(), win_attrs);
    if (!result) {
        throw std::runtime_error(
            std::format(
                "set_dos_attrs_native: Failed to set attributes of \"{}\": {}\n",
                path.string(),
                get_error_message(GetLastError())));
    }
}

}  // namespace netmount_srv

#endif


#if DOS_ATTRS_IN_EXTENDED == 1
#error "The macro DOS_ATTRS_IN_EXTENDED is set to 1, but extended attributes are not supported on Windows."
#endif
