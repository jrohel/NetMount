// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2026 Jaroslav Rohel, jaroslav.rohel@gmail.com

#include "fs.hpp"
#include "logger.hpp"

#if DOS_ATTRS_NATIVE == 1
#error "The macro DOS_ATTRS_NATIVE is set to 1, but native DOS attribute support is not available on macOS."
#endif


#if DOS_ATTRS_IN_EXTENDED == 1

#include <sys/xattr.h>

#define DOS_ATTRS_EA_NAME "user.NetMountAttrs"

namespace netmount_srv {

bool is_dos_attrs_in_extended_supported(const std::filesystem::path & path) {
    // macOS: getxattr(path, name, value, size, position, options)
    const auto ret = getxattr(path.c_str(), DOS_ATTRS_EA_NAME, nullptr, 0, 0, 0);
    if (ret == -1) {
        const auto orig_errno = errno;
        log(LogLevel::DEBUG,
            "is_dos_attrs_in_extended_supported: Failed to fetch attributes of \"{}\": {}\n",
            path.string(),
            strerror(orig_errno));
        if (orig_errno == ENOTSUP) {
            return false;
        }
    }
    return true;
}


uint8_t get_dos_attrs_from_extended(const std::filesystem::path & path) {
    uint8_t attrs[8] = {0};
    // macOS: getxattr(path, name, value, size, position, options)
    const auto ret = getxattr(path.c_str(), DOS_ATTRS_EA_NAME, &attrs, sizeof(attrs), 0, 0);
    if (ret == -1) {
        const auto orig_errno = errno;
        // macOS uses ENOATTR instead of Linux's ENODATA
        if (orig_errno == ENOATTR) {
            return std::filesystem::is_directory(path) ? FAT_NONE : FAT_ARCHIVE;
        }
        throw std::runtime_error(
            std::format(
                "get_dos_attrs_from_extended: Failed to fetch attributes of \"{}\": {}\n",
                path.string(),
                strerror(orig_errno)));
    }
    return attrs[0] & (FAT_ARCHIVE | FAT_HIDDEN | FAT_RO | FAT_SYSTEM);
}


void set_dos_attrs_to_extended(const std::filesystem::path & path, uint8_t attrs) {
    attrs &= FAT_ARCHIVE | FAT_HIDDEN | FAT_RO | FAT_SYSTEM;

    const bool default_attrs = attrs == FAT_NONE      ? std::filesystem::is_directory(path)
                               : attrs == FAT_ARCHIVE ? !std::filesystem::is_directory(path)
                                                      : false;
    if (default_attrs) {
        // macOS: removexattr(path, name, options)
        const auto ret = removexattr(path.c_str(), DOS_ATTRS_EA_NAME, 0);
        if (ret == -1) {
            const auto orig_errno = errno;
            // macOS uses ENOATTR instead of Linux's ENODATA
            if (orig_errno == ENOATTR) {
                return;
            }
            throw std::runtime_error(
                std::format(
                    "set_dos_attrs_to_extended: Failed to remove attributes of \"{}\": {}\n",
                    path.string(),
                    strerror(orig_errno)));
        }
        return;
    }

    // macOS: setxattr(path, name, value, size, position, options)
    const auto ret = setxattr(path.c_str(), DOS_ATTRS_EA_NAME, &attrs, sizeof(attrs), 0, 0);
    if (ret == -1) {
        const auto orig_errno = errno;
        throw std::runtime_error(
            std::format(
                "set_dos_attrs_to_extended: Failed to set attributes of \"{}\": {}\n",
                path.string(),
                strerror(orig_errno)));
    }
}

}  // namespace netmount_srv

#endif
