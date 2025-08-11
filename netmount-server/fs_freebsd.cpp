// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#include "fs.hpp"
#include "logger.hpp"


#if DOS_ATTRS_NATIVE == 1

#include <sys/mount.h> /* statfs() */
#include <sys/stat.h>

namespace {

bool is_on_fat(const std::filesystem::path & path) {
    struct statfs buf;
    const auto res = statfs(path.c_str(), &buf);
    if (res == -1) {
        const auto orig_errno = errno;
        log(LogLevel::DEBUG, "is_on_fat: Failed statfs on \"{}\": {}\n", path.string(), strerror(orig_errno));
        return false;
    }

    log(LogLevel::DEBUG,
        "is_on_fat: statfs reports \"{}\" as the filesystem for \"{}\"\n",
        buf.f_fstypename,
        path.string());

    if (strcmp(buf.f_fstypename, "msdosfs") != 0) {
        return false;
    }

    return true;
}

}  // namespace


namespace netmount_srv {

bool is_dos_attrs_native_supported(const std::filesystem::path & path) { return is_on_fat(path); }


uint8_t get_dos_attrs_native(const std::filesystem::path & path) {
    struct stat statbuf;
    const auto res = stat(path.c_str(), &statbuf);
    if (res == -1) {
        const auto orig_errno = errno;
        // error (probably doesn't exist)
        throw std::runtime_error(
            std::format(
                "get_dos_attrs_native: Failed to fetch attributes of \"{}\": {}\n",
                path.string(),
                strerror(orig_errno)));
    }

    uint8_t attrs = FAT_NONE;
    if (statbuf.st_flags & UF_READONLY) {
        attrs |= FAT_RO;
    }
    if (statbuf.st_flags & UF_HIDDEN) {
        attrs |= FAT_HIDDEN;
    }
    if (statbuf.st_flags & UF_SYSTEM) {
        attrs |= FAT_SYSTEM;
    }
    if (statbuf.st_flags & UF_ARCHIVE) {
        attrs |= FAT_ARCHIVE;
    }
    return attrs;
}


void set_dos_attrs_native(const std::filesystem::path & path, uint8_t attrs) {
    unsigned long flags = 0;
    if (attrs & FAT_RO) {
        flags |= UF_READONLY;
    }
    if (attrs & FAT_HIDDEN) {
        flags |= UF_HIDDEN;
    }
    if (attrs & FAT_SYSTEM) {
        flags |= UF_SYSTEM;
    }
    if (attrs & FAT_ARCHIVE) {
        flags |= UF_ARCHIVE;
    }
    const auto res = chflags(path.c_str(), flags);
    if (res == -1) {
        const auto orig_errno = errno;
        throw std::runtime_error(
            std::format(
                "set_dos_attrs_native: Failed to set attributes of \"{}\": {}\n", path.string(), strerror(orig_errno)));
    }
}

}  // namespace netmount_srv

#endif


#if DOS_ATTRS_IN_EXTENDED == 1

#include <sys/extattr.h>

#define DOS_ATTRS_EA_NAME "NetMountAttrs"

namespace netmount_srv {

bool is_dos_attrs_in_extended_supported(const std::filesystem::path & path) {
    const auto ret = extattr_get_file(path.c_str(), EXTATTR_NAMESPACE_USER, DOS_ATTRS_EA_NAME, NULL, 0);
    if (ret == -1) {
        const auto orig_errno = errno;
        if (orig_errno == ENOATTR) {
            return true;
        }
        log(LogLevel::DEBUG,
            "is_dos_attrs_in_extended_supported: Failed to fetch attributes of \"{}\": {}\n",
            path.string(),
            strerror(orig_errno));
        return false;
    }
    return true;
}


uint8_t get_dos_attrs_from_extended(const std::filesystem::path & path) {
    uint8_t attrs[8] = {0};
    const auto ret = extattr_get_file(path.c_str(), EXTATTR_NAMESPACE_USER, DOS_ATTRS_EA_NAME, &attrs, sizeof(attrs));
    if (ret == -1) {
        const auto orig_errno = errno;
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
        const auto ret = extattr_delete_file(path.c_str(), EXTATTR_NAMESPACE_USER, DOS_ATTRS_EA_NAME);
        if (ret == -1) {
            const auto orig_errno = errno;
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

    const auto ret = extattr_set_file(path.c_str(), EXTATTR_NAMESPACE_USER, DOS_ATTRS_EA_NAME, &attrs, sizeof(attrs));
    if (ret < 0) {
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
