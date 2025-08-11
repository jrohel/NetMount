// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#include "fs.hpp"
#include "logger.hpp"


#if DOS_ATTRS_NATIVE == 1

#include <fcntl.h>
#include <linux/msdos_fs.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace netmount_srv {

bool is_dos_attrs_native_supported(const std::filesystem::path & path) {
    const auto fd = open(path.c_str(), O_RDONLY);
    auto orig_errno = errno;
    if (fd == -1) {
        log(LogLevel::ERROR,
            "is_dos_attr_native_supported: Cannot open file \"{}\": {}\n",
            path.string(),
            strerror(orig_errno));
        return false;
    }
    uint32_t attr;
    const auto res = ioctl(fd, FAT_IOCTL_GET_ATTRIBUTES, &attr);
    orig_errno = errno;
    close(fd);
    if (res == -1) {
        log(LogLevel::DEBUG,
            "is_dos_attr_native_supported: Failed to fetch attributes of \"{}\": {}\n",
            path.string(),
            strerror(orig_errno));
        return false;
    }
    return true;
}


uint8_t get_dos_attrs_native(const std::filesystem::path & path) {
    const auto fd = open(path.c_str(), O_RDONLY);
    auto orig_errno = errno;
    if (fd == -1) {
        throw std::runtime_error(
            std::format("get_dos_attrs_native: Cannot open file \"{}\": {}\n", path.string(), strerror(orig_errno)));
    }

    uint32_t fat_attrs;
    const auto res = ioctl(fd, FAT_IOCTL_GET_ATTRIBUTES, &fat_attrs);
    orig_errno = errno;
    close(fd);
    if (res == -1) {
        throw std::runtime_error(
            std::format(
                "get_dos_attrs_native: Failed to fetch attributes of \"{}\": {}\n",
                path.string(),
                strerror(orig_errno)));
    }

    uint8_t attrs = FAT_NONE;
    if (fat_attrs & ATTR_RO) {
        attrs |= FAT_RO;
    }
    if (fat_attrs & ATTR_HIDDEN) {
        attrs |= FAT_HIDDEN;
    }
    if (fat_attrs & ATTR_SYS) {
        attrs |= FAT_SYSTEM;
    }
    if (fat_attrs & ATTR_ARCH) {
        attrs |= FAT_ARCHIVE;
    }
    return attrs;
}


void set_dos_attrs_native(const std::filesystem::path & path, uint8_t attrs) {
    const auto fd = open(path.c_str(), O_RDONLY);
    auto orig_errno = errno;
    if (fd == -1) {
        throw std::runtime_error(
            std::format("set_dos_attrs_native: Cannot open file \"{}\": {}\n", path.string(), strerror(orig_errno)));
    }

    uint32_t fat_attrs = ATTR_NONE;
    if (attrs & FAT_RO) {
        fat_attrs |= ATTR_RO;
    }
    if (attrs & FAT_HIDDEN) {
        fat_attrs |= ATTR_HIDDEN;
    }
    if (attrs & FAT_SYSTEM) {
        fat_attrs |= ATTR_SYS;
    }
    if (attrs & FAT_ARCHIVE) {
        fat_attrs |= ATTR_ARCH;
    }

    const auto res = ioctl(fd, FAT_IOCTL_SET_ATTRIBUTES, &fat_attrs);
    orig_errno = errno;
    close(fd);
    if (res == -1) {
        throw std::runtime_error(
            std::format(
                "set_dos_attrs_native: Failed to set attributes of \"{}\": {}\n", path.string(), strerror(orig_errno)));
    }
}

}  // namespace netmount_srv

#endif


#if DOS_ATTRS_IN_EXTENDED == 1

#include <sys/xattr.h>

#define DOS_ATTRS_EA_NAME "user.NetMountAttrs"

namespace netmount_srv {

bool is_dos_attrs_in_extended_supported(const std::filesystem::path & path) {
    const auto ret = getxattr(path.c_str(), DOS_ATTRS_EA_NAME, nullptr, 0);
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
    const auto ret = getxattr(path.c_str(), DOS_ATTRS_EA_NAME, &attrs, sizeof(attrs));
    if (ret == -1) {
        const auto orig_errno = errno;
        if (orig_errno == ENODATA) {
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
        const auto ret = removexattr(path.c_str(), DOS_ATTRS_EA_NAME);
        if (ret == -1) {
            const auto orig_errno = errno;
            if (orig_errno == ENODATA) {
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

    const auto ret = setxattr(path.c_str(), DOS_ATTRS_EA_NAME, &attrs, sizeof(attrs), 0);
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
