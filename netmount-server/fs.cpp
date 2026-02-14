// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025-2026 Jaroslav Rohel, jaroslav.rohel@gmail.com

#include "fs.hpp"

#include "logger.hpp"
#include "unicode_to_ascii.hpp"
#include "utils.hpp"

#include <errno.h>
#ifdef __linux__
#include <fcntl.h>
#include <linux/msdos_fs.h>
#endif
#include <stdio.h>
#ifdef __linux__
#include <sys/ioctl.h>
#endif
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <chrono>
#include <compare>
#include <exception>
#include <format>
#include <string_view>


std::strong_ordering operator<=>(const fcb_file_name & lhs, const fcb_file_name & rhs) noexcept {
    auto ret = strncmp(
        reinterpret_cast<const char *>(lhs.name_blank_padded),
        reinterpret_cast<const char *>(rhs.name_blank_padded),
        sizeof(lhs.name_blank_padded));
    if (ret == 0) {
        ret = strncmp(
            reinterpret_cast<const char *>(lhs.ext_blank_padded),
            reinterpret_cast<const char *>(rhs.ext_blank_padded),
            sizeof(lhs.ext_blank_padded));
    }
    return ret == 0 ? std::strong_ordering::equal
                    : (ret < 0 ? std::strong_ordering::less : std::strong_ordering::greater);
}


bool operator==(const fcb_file_name & lhs, const fcb_file_name & rhs) noexcept { return (lhs <=> rhs) == 0; }


namespace netmount_srv {

namespace {

// Fills the DosFileProperties structure if `properties` != nullptr.
// Returns DOS attributes for `path` or FAT_ERROR_ATTR on error.
// DOS attr flags: 1=RO 2=HID 4=SYS 8=VOL 16=DIR 32=ARCH 64=DEVICE
uint8_t get_path_dos_properties(const std::filesystem::path & path, DosFileProperties * properties, AttrsMode mode);

// Sets attributes `attrs` on file defined by `path`.
// Throws exception on error.
void set_item_attrs(const std::filesystem::path & path, uint8_t attrs, AttrsMode mode);

// Gets attributes of file defined by `path`.
// Throws exception on error.
uint8_t get_item_attrs(const std::filesystem::path & path, AttrsMode mode);

// Creates directory `dir`
// Throws exception on error.
void make_dir(const std::filesystem::path & dir);

// Removes directory `dir`
// Throws exception on error.
void delete_dir(const std::filesystem::path & dir);

// Changes the current working directory to `dir`
// Throws exception on error.
void change_dir(const std::filesystem::path & dir);

// Creates or truncates a file `path` with attributes `attrs`.
// Returns properties of created/truncated file.
// Throws exception on error.
DosFileProperties create_or_truncate_file(const std::filesystem::path & path, uint8_t attrs, AttrsMode mode);

// Resize file
// Throws exception on error.
void resize_file(const std::filesystem::path & path, uint32_t new_size);

// Removes `file`
// Throws exception on error.
void delete_file(const std::filesystem::path & file);

// Renames `old_name` to `new_name`
// Throws exception on error or if no matching file found
void rename_file(const std::filesystem::path & old_name, const std::filesystem::path & new_name);

// Returns filesystem total size and free space in bytes, or 0, 0 on error
std::pair<uint64_t, uint64_t> fs_space_info(const std::filesystem::path & path);

// Converts lowercase ascii characters to uppercase and removes illegal characters
// Returns new length and true if file name was shortened
std::pair<unsigned int, bool> sanitize_short_name(std::string_view in, char * out_buf, unsigned int buf_size);

// Converts server file name to DOS short name in FCB format
bool file_name_to_83(
    const std::filesystem::path & orig_name, fcb_file_name & fcb_name, std::set<fcb_file_name> & used_names);


// Tests whether the FCB file name matches the FCB file mask.
bool match_fcb_name_to_mask(const fcb_file_name & mask, const fcb_file_name & name) {
    for (unsigned int i = 0; i < sizeof(name.name_blank_padded); ++i) {
        if ((ascii_to_upper(name.name_blank_padded[i]) != ascii_to_upper(mask.name_blank_padded[i])) &&
            (mask.name_blank_padded[i] != '?')) {
            return false;
        }
    }
    for (unsigned int i = 0; i < sizeof(name.ext_blank_padded); ++i) {
        if ((ascii_to_upper(name.ext_blank_padded[i]) != ascii_to_upper(mask.ext_blank_padded[i])) &&
            (mask.ext_blank_padded[i] != '?')) {
            return false;
        }
    }
    return true;
}


// Converts a time_t into a FAT style timestamp
// 5 bits 00–04: Seconds (0–29, with each unit representing 2 seconds)
// 6 bits 05–10: Minutes (0–59)
// 5 bits 11–15: Hours (0–23)
// 5 bits 16–20: Day (1–31)
// 4 bits 21–24: Month (1–12)
// 7 bits 25–31: Year (since 1980, with 0 representing 1980, 1 representing 1981, and so on).
uint32_t time_to_fat(time_t t) {
    uint32_t res;
    struct tm * ltime;
    ltime = localtime(&t);
    if (ltime->tm_year < 80) {
        // 1980-01-01 00:00:00 - DOS FAT minimum timestamp
        return ((1U << 5) + 1U) << 16;
    }
    if (ltime->tm_year > 207) {
        // 2107-12-31 23:59:58 - DOS FAT maximu timestamp
        return (((0x7FU << 9) + (12U << 5) + 31U) << 16) + (23U << 11) + (59U << 5) + (58U >> 1);
    }
    res = ltime->tm_year - 80;  // tm_year is years from 1900, FAT is years from 1980
    res <<= 4;
    res |= ltime->tm_mon + 1;  // tm_mon is in range 0..11 while FAT expects 1..12
    res <<= 5;
    res |= ltime->tm_mday;
    res <<= 5;
    res |= ltime->tm_hour;
    res <<= 6;
    res |= ltime->tm_min;
    res <<= 5;
    res |= ltime->tm_sec / 2;  // DOS stores seconds divided by two
    return res;
}

}  // namespace


void Drive::set_root(std::filesystem::path root) {
    if (used) {
        throw std::runtime_error("already used");
    }
    this->root = std::move(root);
    used = true;
}


void Drive::set_volume_label(const std::string & label) {
    if (label.empty()) {
        has_volume_label = false;
        log(LogLevel::DEBUG, "set_volume_label: Remove label\n");
        return;
    }

    // clear previos value
    for (auto & ch : volume_label.name_blank_padded) {
        ch = ' ';
    }
    for (auto & ch : volume_label.ext_blank_padded) {
        ch = ' ';
    }

    // copy/convert to FCB file name style
    unsigned int i = 0;
    auto it = label.begin();
    while (it != label.end() && i < sizeof(volume_label.name_blank_padded)) {
        volume_label.name_blank_padded[i++] = *it++;
    }
    i = 0;
    while (it != label.end() && i < sizeof(volume_label.ext_blank_padded)) {
        volume_label.ext_blank_padded[i++] = *it++;
    }

    has_volume_label = true;

    log(LogLevel::DEBUG,
        "set_volume_label: Set label \"{:.8s}{:.3s}\"\n",
        reinterpret_cast<const char *>(volume_label.name_blank_padded),
        reinterpret_cast<const char *>(volume_label.ext_blank_padded));
}


uint16_t Drive::get_handle(const std::filesystem::path & server_path) {
    uint16_t first_free = items.size();
    uint16_t oldest = 0;
    const time_t now = time(NULL);

    // see if not already in cache
    for (uint16_t handle = 0; handle < items.size(); ++handle) {
        auto & cur_item = items[handle];

        if (cur_item.path == server_path) {
            cur_item.last_used_time = now;
            log(LogLevel::DEBUG,
                "get_handle: Found handle {} with path \"{}\" in cache\n",
                handle,
                server_path.string());
            return handle;
        }

        if ((now - cur_item.last_used_time) > 3600) {
            if (!cur_item.directory_list.empty()) {
                // Directory list is too old -> remove it from cache and free memory.
                // It will be re-generated if necessary.
                log(LogLevel::DEBUG,
                    "get_handle: Remove old directory list for handle {} path \"{}\" from cache\n",
                    handle,
                    server_path.string());
                cur_item.directory_list = {};
            }
        }

        if (first_free == items.size()) {
            if (cur_item.path.empty()) {
                first_free = handle;
            } else if (items[oldest].last_used_time > cur_item.last_used_time) {
                oldest = handle;
            }
        }
    }

    if (first_free == items.size()) {
        // not found - no free slot available
        if (first_free < MAX_HANDLE_COUNT) {
            // allocate new slot
            items.resize(first_free + 1);
        } else {
            // all handles are used, pick the oldest one and replace it
            items[oldest].path.clear();
            items[oldest].directory_list = {};
            first_free = oldest;
        }
    }

    // assign item to handle
    items[first_free].path = server_path;
    items[first_free].last_used_time = now;

    return first_free;
}


Drive::Item & Drive::get_item(uint16_t handle) {
    if (handle >= items.size()) {
        throw FilesystemError(
            std::format("Handle {} is invalid - only {} handles are currently allocated", handle, items.size()),
            DOS_EXTERR_INVALID_HANDLE);
    }
    Item & item = items[handle];
    if (item.path.empty()) {
        throw FilesystemError(
            std::format("Handle {} is invalid because it is empty", handle), DOS_EXTERR_INVALID_HANDLE);
    }
    return item;
}


const std::filesystem::path & Drive::get_handle_path(uint16_t handle) {
    auto & item = get_item(handle);
    const auto & path = item.path;
    item.update_last_used_timestamp();
    return path;
}


int32_t Drive::read_file(void * buffer, uint16_t handle, uint32_t offset, uint16_t len) {
    auto & item = get_item(handle);
    const auto & fname = item.path;

    item.update_last_used_timestamp();

#ifdef _WIN32
    auto * const fd = _wfopen(fname.c_str(), L"rb");
#else
    auto * const fd = fopen(fname.c_str(), "rb");
#endif
    if (!fd) {
        throw FilesystemError(std::format("Cannot open file: {}", strerror(errno)), DOS_EXTERR_ACCESS_DENIED);
    }

    if (fseek(fd, offset, SEEK_SET) != 0) {
        const auto orig_errno = errno;
        fclose(fd);
        throw FilesystemError(std::format("Cannot seek in file: {}", strerror(orig_errno)), DOS_EXTERR_SEEK_ERROR);
    }

    const auto res = fread(buffer, 1, len, fd);

    fclose(fd);

    return static_cast<int32_t>(res);
}


int32_t Drive::write_file(const void * buffer, uint16_t handle, uint32_t offset, uint16_t len) {
    auto & item = get_item(handle);
    const auto & fname = item.path;

    item.update_last_used_timestamp();

    // READ_ONLY is handled at open time. Do not check it here.
    // Files opened with CREATE_FILE (create or truncate) must remain writable.
    // Checking it here would wrongly block writes to a newly created/truncated file.
    //if (get_server_path_attrs(fname) & FAT_RO) {
    //    throw FilesystemError(
    //        std::format("Access denied: File \"{}\" has the READ_ONLY attribute", fname.string()),
    //        DOS_EXTERR_ACCESS_DENIED);
    //}

    // len 0 means "truncate" or "extend"
    if (len == 0) {
        log(LogLevel::DEBUG, "write_file: truncate \"{}\" to {} bytes\n", fname.string(), offset);
        resize_file(fname, offset);
        return 0;
    }

    //  write to file
    log(LogLevel::DEBUG, "write_file: write {} bytes into file \"{}\" at offset {}\n", len, fname.string(), offset);
#ifdef _WIN32
    auto * const fd = _wfopen(fname.c_str(), L"r+b");
#else
    auto * const fd = fopen(fname.c_str(), "r+b");
#endif
    if (!fd) {
        throw FilesystemError(std::format("Cannot open file: {}", strerror(errno)), DOS_EXTERR_ACCESS_DENIED);
    }

    if (fseek(fd, offset, SEEK_SET) != 0) {
        const auto orig_errno = errno;
        fclose(fd);
        throw FilesystemError(std::format("Cannot seek in file: {}", strerror(orig_errno)), DOS_EXTERR_SEEK_ERROR);
    }

    const auto res = fwrite(buffer, 1, len, fd);

    fclose(fd);

    return static_cast<int32_t>(res);
}


int32_t Drive::get_file_size(uint16_t handle) {
    auto & item = get_item(handle);

    DosFileProperties fprops;
    if (get_path_dos_properties(item.path, &fprops, AttrsMode::IGNORE) == FAT_ERROR_ATTR) {
        return -1;
    }

    item.update_last_used_timestamp();

    return fprops.size;
}


bool Drive::find_file(
    uint16_t handle, const fcb_file_name & tmpl, unsigned char attr, DosFileProperties & properties, uint16_t & nth) {

    if (nth == 0 && attr == FAT_VOLUME) {
        // Handle volume label directly; no need to process directory list.

        if (!has_volume_label) {
            log(LogLevel::DEBUG, "find_file: Drive has no volume label\n");
            return false;
        }
        if (!match_fcb_name_to_mask(tmpl, volume_label)) {
            log(LogLevel::DEBUG, "find_file: Drive volume label does not match mask\n");
            return false;
        }

        properties.fcb_name = volume_label;
        properties.attrs = FAT_VOLUME;
        properties.size = 0;
        properties.time_date = 0;

        log(LogLevel::DEBUG,
            "find_file: Found volume label: {:.8s}{:.3s}\n",
            reinterpret_cast<const char *>(volume_label.name_blank_padded),
            reinterpret_cast<const char *>(volume_label.ext_blank_padded));

        nth = 1;
        return true;
    }

    auto & item = get_item(handle);

    // recompute the dir listing if operation is FIND_FIRST (nth == 0) or if no cache found
    if ((nth == 0) || (item.directory_list.empty())) {
        const auto count = item.create_directory_list(*this);
        if (count < 0) {
            log(LogLevel::WARNING, "Failed to scan dir \"{}\"\n", item.path.string());
            return false;
        } else {
            log(LogLevel::DEBUG, "Scanned dir \"{}\", found {} items\n", item.path.string(), count);
            if (global_log_level >= LogLevel::TRACE) {
                for (const auto & item : item.directory_list) {
                    log(LogLevel::TRACE,
                        "  \"{:.8s}{:.3s}\", attr 0x{:02X}, {} bytes\n",
                        reinterpret_cast<const char *>(&item.fcb_name.name_blank_padded),
                        reinterpret_cast<const char *>(&item.fcb_name.ext_blank_padded),
                        item.attrs,
                        item.size);
                }
            }
        }
    }

    DosFileProperties const * found_props{nullptr};
    auto & dir_list = item.directory_list;
    const auto item_count = dir_list.size();
    uint16_t n;
    for (n = nth; n < item_count; ++n) {
        const auto & item_props = dir_list[n];

        if (!match_fcb_name_to_mask(tmpl, item_props.fcb_name))
            continue;

        if (attr == FAT_VOLUME) {
            // look only for VOLUME -> skip if not VOLUME
            if ((item_props.attrs & FAT_VOLUME) == 0) {
                continue;
            }
        } else {
            // return only file with at most the specified combination of hidden, system, and directory attributes
            if ((attr | (item_props.attrs & (FAT_HIDDEN | FAT_SYSTEM | FAT_VOLUME | FAT_DIRECTORY))) != attr)
                continue;
        }

        found_props = &item_props;
        break;
    }

    if (found_props) {
        nth = n + 1;
        properties = *found_props;
        return true;
    }

    return false;
}


const std::filesystem::path & Drive::get_server_name(
    uint16_t handle, const fcb_file_name & fcb_name, bool create_directory_list) {
    static const std::filesystem::path empty_path;
    auto & item = items[handle];
    if (create_directory_list || item.directory_list.empty()) {
        item.create_directory_list(*this);
    }
    for (auto & dir : item.directory_list) {
        if (dir.attrs != FAT_VOLUME && dir.fcb_name == fcb_name) {
            return dir.server_name;
        }
    }
    return empty_path;
}


std::pair<std::filesystem::path, bool> Drive::create_server_path(
    const std::filesystem::path & client_path, bool create_directory_list) {
    const auto & root = get_root();

    if (client_path.empty()) {
        return {root, true};
    }

    if (get_file_name_conversion() == Drive::FileNameConversion::OFF) {
        auto server_path = root / client_path;
        if (!std::filesystem::exists(server_path.parent_path())) {
            throw FilesystemError(
                std::format("create_server_path: Parent path not found: {}", server_path.parent_path().string()),
                DOS_EXTERR_PATH_NOT_FOUND);
        }
        return {server_path, std::filesystem::exists(server_path) || std::filesystem::is_symlink(server_path)};
    }

    std::filesystem::path server_path = root;
    auto it = client_path.begin();
    auto it_end = client_path.end();
    while (true) {
        const fcb_file_name fcb_name = short_name_to_fcb(it->string());
        auto & server_name = get_server_name(get_handle(server_path), fcb_name, create_directory_list);
        auto prev_it = it;
        ++it;
        if (server_name.empty()) {
            if (it == it_end) {
                server_path /= *prev_it;
                return {server_path, false};
            }
            throw FilesystemError(
                std::format("create_server_path: Parent path not found: {}", (server_path / *prev_it).string()),
                DOS_EXTERR_PATH_NOT_FOUND);
        }
        server_path /= server_name;
        if (it == it_end) {
            return {server_path, true};
        }
    }
}


void Drive::make_dir(const std::filesystem::path & client_path) {
    auto [server_path, exist] = create_server_path(client_path);
    if (exist) {
        throw FilesystemError("make_dir: Directory exists: " + server_path.string(), DOS_EXTERR_ACCESS_DENIED);
    }
    netmount_srv::make_dir(server_path);

    // Recreates directory_list
    create_server_path(client_path, true);
}


void Drive::delete_dir(const std::filesystem::path & client_path) {
    auto [server_path, exist] = create_server_path(client_path);
    if (!exist) {
        throw FilesystemError(
            "delete_dir: Directory does not exist: " + server_path.string(), DOS_EXTERR_PATH_NOT_FOUND);
    }

    if (get_server_path_attrs(server_path) & FAT_RO) {
        throw FilesystemError("Access denied: Directory has the READ_ONLY attribute", DOS_EXTERR_ACCESS_DENIED);
    }

    netmount_srv::delete_dir(server_path);

    // Recreates directory_list
    create_server_path(client_path, true);
}


void Drive::change_dir(const std::filesystem::path & client_path) {
    auto [server_path, exist] = create_server_path(client_path);
    if (!exist) {
        throw FilesystemError(
            "change_dir: Directory does not exist: " + server_path.string(), DOS_EXTERR_PATH_NOT_FOUND);
    }
    netmount_srv::change_dir(server_path);
}


void Drive::set_item_attrs(const std::filesystem::path & client_path, uint8_t attrs) {
    const auto attrs_mode = get_attrs_mode();
    if (attrs_mode != AttrsMode::IGNORE) {
        auto [server_path, exist] = create_server_path(client_path);
        netmount_srv::set_item_attrs(server_path, attrs, attrs_mode);

        // Recreates directory_list
        create_server_path(client_path, true);
    }
}


uint8_t Drive::get_server_path_attrs(const std::filesystem::path & server_path) {
    return get_item_attrs(server_path, get_attrs_mode());
}


uint8_t Drive::get_dos_properties(const std::filesystem::path & client_path, DosFileProperties * properties) {
    auto [server_path, exist] = create_server_path(client_path);
    if (!exist) {
        throw FilesystemError(
            std::format("get_dos_properties: File not found: {}", server_path.string()), DOS_EXTERR_FILE_NOT_FOUND);
    }

    auto attrs = get_server_path_dos_properties(server_path, properties);
    if (attrs == FAT_ERROR_ATTR) {
        throw FilesystemError(
            std::format("get_dos_properties: get attributes failed: {}", server_path.string()),
            DOS_EXTERR_FILE_NOT_FOUND);
    }

    return attrs;
}


uint8_t Drive::get_server_path_dos_properties(
    const std::filesystem::path & server_path, DosFileProperties * properties) {
    return get_path_dos_properties(server_path, properties, get_attrs_mode());
}


void Drive::rename_file(const std::filesystem::path & old_client_path, const std::filesystem::path & new_client_path) {
    const auto [old_server_path, exist1] = create_server_path(old_client_path);
    const auto [new_server_path, exist2] = create_server_path(new_client_path);
    netmount_srv::rename_file(old_server_path, new_server_path);

    // Recreates directory_list
    create_server_path(new_client_path, true);
}


void Drive::delete_files(const std::filesystem::path & client_pattern) {
    const auto [server_path, exist] = create_server_path(client_pattern);

    if (exist) {
        uint8_t attrs = 0;
        try {
            attrs = get_server_path_attrs(server_path);
        } catch (const std::runtime_error &) {
        }
        if (attrs & FAT_RO) {
            throw FilesystemError(
                std::format("Access denied: File \"{}\" has the READ_ONLY attribute", server_path.string()),
                DOS_EXTERR_ACCESS_DENIED);
        }
        netmount_srv::delete_file(server_path);
        return;
    }

    // test if pattern contains '?' characters
    bool is_pattern = false;
    const std::string & pattern_string = server_path.string();
    for (auto ch : pattern_string) {
        if (ch == '?') {
            is_pattern = true;
            break;
        }
    }
    if (!is_pattern) {
        throw FilesystemError("delete_files: File does not exist: " + server_path.string(), DOS_EXTERR_FILE_NOT_FOUND);
    }

    // if pattern, get directory and file parts and iterate over all directory
    const std::filesystem::path directory = server_path.parent_path();
    const std::string filemask = client_pattern.filename().string();

    const auto filfcb = short_name_to_fcb(filemask);

    if (get_file_name_conversion() == Drive::FileNameConversion::OFF) {
        // If file name conversion is turned off, we traverse the file system directly.
        for (const auto & dentry : std::filesystem::directory_iterator(directory)) {
            if (dentry.is_directory()) {
                // skip directories
                continue;
            }

            // if match, delete the file
            const auto & path_str = dentry.path().string();
            if (match_fcb_name_to_mask(filfcb, short_name_to_fcb(path_str))) {
                uint8_t attrs = 0;
                try {
                    attrs = get_server_path_attrs(dentry.path());
                } catch (const std::runtime_error &) {
                }
                if (attrs & FAT_RO) {
                    log(LogLevel::WARNING,
                        "Access denied: File \"{}\" has the READ_ONLY attribute",
                        dentry.path().string());
                    continue;
                }
                std::error_code ec;
                if (!std::filesystem::remove(dentry.path(), ec)) {
                    log(LogLevel::ERROR, "delete_files: Failed to delete file \"{}\": {}\n", path_str, ec.message());
                }
            }
        }
        return;
    }

    const auto handle = get_handle(directory);
    const auto & item = items[handle];

    // iterate over the directory_list and delete files that match the pattern
    for (const auto & file_properties : item.directory_list) {
        if (file_properties.attrs & FAT_DIRECTORY) {
            // skip directories
            continue;
        }

        if (match_fcb_name_to_mask(filfcb, file_properties.fcb_name)) {
            const auto path = directory / file_properties.server_name;
            uint8_t attrs = 0;
            try {
                attrs = get_server_path_attrs(path);
            } catch (const std::runtime_error &) {
            }
            if (attrs & FAT_RO) {
                log(LogLevel::WARNING, "Access denied: File \"{}\" has the READ_ONLY attribute", path.string());
                continue;
            }
            try {
                netmount_srv::delete_file(path);
            } catch (const std::runtime_error & ex) {
                log(LogLevel::ERROR, "delete_files: Failed to delete file \"{}\": {}\n", path.string(), ex.what());
            }
        }
    }
}


DosFileProperties Drive::create_or_truncate_file(const std::filesystem::path & server_path, uint8_t attrs) {
    return netmount_srv::create_or_truncate_file(server_path, attrs, get_attrs_mode());
}


std::pair<uint64_t, uint64_t> Drive::space_info() {
    const auto & root = get_root();
    if (root.empty()) {
        throw std::runtime_error("space_info: Not shared drive");
    }
    return netmount_srv::fs_space_info(root);
}


int32_t Drive::Item::create_directory_list(const Drive & drive) {
    directory_list.clear();
    fcb_names.clear();

    try {
        for (const auto & dentry : std::filesystem::directory_iterator(path)) {
            if (directory_list.empty()) {
                std::error_code ec;
                const bool is_root_dir = std::filesystem::equivalent(path, drive.get_root(), ec);
                if (ec) {
                    log(LogLevel::ERROR, "create_directory_list: {}\n", ec.message());
                    return -1;
                }
                if (is_root_dir) {
                    if (drive.has_volume_label) {
                        DosFileProperties fprops;
                        fprops.fcb_name = drive.volume_label;
                        fprops.attrs = FAT_VOLUME;
                        fprops.size = 0;
                        fprops.time_date = 0;
                        log(LogLevel::DEBUG,
                            "create_directory_list: VOLUME LABEL {:.8s}{:.3s} -> {:.8s} {:.3s}\n",
                            reinterpret_cast<const char *>(drive.volume_label.name_blank_padded),
                            reinterpret_cast<const char *>(drive.volume_label.ext_blank_padded),
                            reinterpret_cast<const char *>(fprops.fcb_name.name_blank_padded),
                            reinterpret_cast<const char *>(fprops.fcb_name.ext_blank_padded));
                        directory_list.emplace_back(fprops);
                    }
                } else {
                    // Add the . and .. entries to non-root directories
                    for (const auto name : {".", ".."}) {
                        const auto fullpath = path / name;
                        DosFileProperties fprops;
                        get_path_dos_properties(fullpath, &fprops, drive.get_attrs_mode());
                        fprops.fcb_name = short_name_to_fcb(name);
                        if (drive.get_file_name_conversion() != Drive::FileNameConversion::OFF) {
                            fprops.server_name = name;
                        }
                        log(LogLevel::DEBUG,
                            "create_directory_list: {} -> {:.8s} {:.3s}\n",
                            name,
                            reinterpret_cast<const char *>(fprops.fcb_name.name_blank_padded),
                            reinterpret_cast<const char *>(fprops.fcb_name.ext_blank_padded));
                        directory_list.emplace_back(fprops);
                    }
                }
            } else if (directory_list.size() == 0xFFFFU) {
                // DOS FIND uses a 16-bit offset for directory entries, we cannot address more than 65535 entries.
                log(LogLevel::ERROR,
                    "FilesystemDB::Item::create_directory_list: Directory \"{}\" contains more than 65535 items",
                    path.string());
                break;
            }

            DosFileProperties fprops;
            auto path = dentry.path();
            auto filename = path.filename();
            get_path_dos_properties(path, &fprops, drive.get_attrs_mode());
            if (drive.get_file_name_conversion() != Drive::FileNameConversion::OFF) {
                file_name_to_83(filename, fprops.fcb_name, fcb_names);
                fprops.server_name = filename;
            }
            log(LogLevel::DEBUG,
                "create_directory_list: {} -> {:.8s} {:.3s}\n",
                filename.string(),
                reinterpret_cast<const char *>(fprops.fcb_name.name_blank_padded),
                reinterpret_cast<const char *>(fprops.fcb_name.ext_blank_padded));
            directory_list.emplace_back(fprops);
        }
    } catch (const std::runtime_error & ex) {
        log(LogLevel::WARNING, "create_directory_list: {}\n", ex.what());
        return -1;
    }

    update_last_used_timestamp();

    return directory_list.size();
}


void Drive::Item::update_last_used_timestamp() { last_used_time = time(NULL); }


fcb_file_name short_name_to_fcb(const std::string & short_name) noexcept {
    fcb_file_name fcb_name;
    unsigned int i = 0;
    auto it = short_name.begin();
    const auto it_end = short_name.end();
    while (it != it_end && *it == '.') {
        fcb_name.name_blank_padded[i++] = '.';
        ++it;
        if (i == 2) {
            break;
        }
    }
    while (it != it_end && *it != '.') {
        fcb_name.name_blank_padded[i++] = ascii_to_upper(*it);
        ++it;
        if (i == sizeof(fcb_name.name_blank_padded)) {
            break;
        }
    }
    for (; i < sizeof(fcb_name.name_blank_padded); ++i) {
        fcb_name.name_blank_padded[i] = ' ';
    }

    // move to dot
    while (it != it_end && *it != '.') {
        ++it;
    }

    // skip the dot
    if (it != it_end) {
        ++it;
    }

    i = 0;
    for (; it != it_end && *it != '.'; ++it) {
        fcb_name.ext_blank_padded[i++] = ascii_to_upper(*it);
        if (i == sizeof(fcb_name.ext_blank_padded)) {
            break;
        }
    }

    for (; i < sizeof(fcb_name.ext_blank_padded); ++i) {
        fcb_name.ext_blank_padded[i] = ' ';
    }

    return fcb_name;
}


namespace {

std::pair<unsigned int, bool> sanitize_short_name(std::string_view in, char * out_buf, unsigned int buf_size) {
    // Allowed special characters
    static const std::set<char> allowed_special = {
        '!', '#', '$', '%', '&', '\'', '(', ')', '-', '@', '^', '_', '`', '{', '}', '~'};

    const std::size_t last_non_space_idx = in.find_last_not_of(' ');

    unsigned int out_len = 0;
    for (std::size_t idx = 0; idx < in.length(); ++idx) {
        const char ch = in[idx];
        if (out_len == buf_size) {
            return {out_len, true};
        }
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || allowed_special.contains(ch)) {
            out_buf[out_len++] = ch;
            continue;
        }
        if (ch >= 'a' && ch <= 'z') {
            out_buf[out_len++] = ch - 'a' + 'A';
            continue;
        }

        // Spaces are allowed, but trailing spaces in the base name or extension
        // are considered padding and are not part of the file name.
        if (ch == ' ' && idx < last_non_space_idx) {
            out_buf[out_len++] = ch;
            continue;
        }

        // Replace disallowed characters with '_'
        out_buf[out_len++] = '_';
    }

    // pad with spaces
    while (out_len < buf_size) {
        out_buf[--buf_size] = ' ';
    }

    return {out_len, false};
}


bool file_name_to_83(
    const std::filesystem::path & orig_name, fcb_file_name & fcb_name, std::set<fcb_file_name> & used_names) {
#ifdef _WIN32
    const std::string long_name = convert_windows_unicode_to_ascii(orig_name.wstring());
#else
    const std::string long_name = convert_utf8_to_ascii(orig_name.string());
#endif
    const size_t dotPos = long_name.find_last_of('.');
    std::string_view base;
    std::string_view ext;
    if (dotPos != std::string::npos) {
        base = std::string_view(long_name.begin(), long_name.begin() + dotPos);
        ext = std::string_view(long_name.begin() + dotPos + 1, long_name.end());
    } else {
        base = long_name;
    }

    auto * const name_blank_padded = reinterpret_cast<char *>(fcb_name.name_blank_padded);
    auto * const ext_blank_padded = reinterpret_cast<char *>(fcb_name.ext_blank_padded);

    auto [base_len, base_shortened] = sanitize_short_name(base, name_blank_padded, sizeof(fcb_name.name_blank_padded));
    auto [ext_len, ext_shortened] = sanitize_short_name(ext, ext_blank_padded, sizeof(fcb_name.ext_blank_padded));

    if (!base_shortened && !ext_shortened && used_names.insert(fcb_name).second) {
        return true;
    }

    // add suffix number
    for (unsigned int counter = 1; counter < 9999; ++counter) {
        const unsigned int counter_len = counter > 999 ? 4 : (counter > 99 ? 3 : (counter > 9 ? 2 : 1));
        if (base_len + counter_len > sizeof(fcb_name.name_blank_padded) - 1) {
            base_len = sizeof(fcb_name.name_blank_padded) - 1 - counter_len;
        }

        name_blank_padded[base_len] = '~';
        char * it_first = name_blank_padded + base_len + 1;
        char * it_last = name_blank_padded + sizeof(fcb_name.name_blank_padded);
        std::to_chars(it_first, it_last, counter);

        if (used_names.insert(fcb_name).second) {
            return true;
        }
    }

    // Error: More then 9999 names with the same prefix
    return false;
}


uint8_t get_path_dos_properties(
    const std::filesystem::path & path, DosFileProperties * properties, [[maybe_unused]] AttrsMode mode) {
    std::error_code ec;
    uint8_t attrs = std::filesystem::is_directory(path, ec) ? FAT_DIRECTORY : 0;
    if (ec) {
        // It may be a symlink pointing to a nonexistent path
        if (!std::filesystem::is_symlink(path, ec)) {
            return FAT_ERROR_ATTR;  // error (is not a symlink, probably the path doesn't exist)
        }
    }

    if (properties) {
        // set file fcbname to the file part of path (ignore traling directory separators)
        auto it = path.end();
        while (it != path.begin() && (--it)->empty()) {
        }
        properties->fcb_name = short_name_to_fcb(it->string());

        time_t seconds = 0;
        std::error_code ec;
        auto ftime = std::filesystem::last_write_time(path, ec);
        if (!ec) {
#if __cpp_lib_chrono >= 201907L
            // C++20 and newer
            // This path uses std::chrono::clock_cast, which is the preferred and safest method.
            auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
#else
            // Fallback for older compilers lacking C++20 support
            // This manual conversion is less robust but works as an alternative.
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::chrono::file_clock::now() + std::chrono::system_clock::now());
#endif
            seconds = std::chrono::system_clock::to_time_t(sctp);
        }
        properties->time_date = time_to_fat(seconds);

        properties->attrs = attrs;

        if (attrs == FAT_DIRECTORY) {
            properties->size = 0;
        } else {
            properties->size = std::filesystem::file_size(path, ec);
            if (ec) {
                properties->size = 0;
            }
        }
    }

    if (ec) {
        return attrs;
    }

    try {
        attrs |= get_item_attrs(path, mode);
        if (properties) {
            properties->attrs = attrs;
        }
        return attrs;
    } catch (const std::runtime_error & ex) {
        log(LogLevel::ERROR, "get_path_dos_properties: {}\n", ex.what());
    }

    return FAT_ERROR_ATTR;
}


void set_item_attrs(
    [[maybe_unused]] const std::filesystem::path & path,
    [[maybe_unused]] uint8_t attrs,
    [[maybe_unused]] AttrsMode mode) {
#if DOS_ATTRS_NATIVE == 1
    if (mode == AttrsMode::NATIVE) {
        set_dos_attrs_native(path, attrs);
    }
#endif

#if DOS_ATTRS_IN_EXTENDED == 1
    if (mode == AttrsMode::IN_EXTENDED) {
        set_dos_attrs_to_extended(path, attrs);
    }
#endif
}


uint8_t get_item_attrs([[maybe_unused]] const std::filesystem::path & path, [[maybe_unused]] AttrsMode mode) {
#if DOS_ATTRS_NATIVE == 1
    if (mode == AttrsMode::NATIVE) {
        return get_dos_attrs_native(path);
    }
#endif

#if DOS_ATTRS_IN_EXTENDED == 1
    if (mode == AttrsMode::IN_EXTENDED) {
        return get_dos_attrs_from_extended(path);
    }
#endif

    return std::filesystem::is_directory(path) ? 0 : FAT_ARCHIVE;
}


void make_dir(const std::filesystem::path & dir) {
    if (!std::filesystem::create_directory(dir)) {
        throw FilesystemError("make_dir: Directory exists: " + dir.string(), DOS_EXTERR_ACCESS_DENIED);
    }
}


void delete_dir(const std::filesystem::path & dir) {
    if (!std::filesystem::exists(dir)) {
        throw FilesystemError("delete_dir: Directory does not exist: " + dir.string(), DOS_EXTERR_PATH_NOT_FOUND);
    }
    if (!std::filesystem::is_directory(dir)) {
        throw FilesystemError("delete_dir: Not a directory: " + dir.string(), DOS_EXTERR_ACCESS_DENIED);
    }
    std::filesystem::remove(dir);
}


void change_dir(const std::filesystem::path & dir) { std::filesystem::current_path(dir); }


DosFileProperties create_or_truncate_file(const std::filesystem::path & path, uint8_t attrs, AttrsMode mode) {
    // try to create/truncate the file
#ifdef _WIN32
    auto * const fd = _wfopen(path.c_str(), L"wb");
#else
    auto * const fd = fopen(path.c_str(), "wb");
#endif
    if (!fd) {
        throw FilesystemError(std::format("Cannot open file: {}", strerror(errno)), DOS_EXTERR_ACCESS_DENIED);
    }
    fclose(fd);

    // set FAT attributes
    if (mode != AttrsMode::IGNORE) {
        try {
            set_item_attrs(path, attrs, mode);
        } catch (const std::runtime_error & ex) {
            log(LogLevel::ERROR,
                "create_or_truncate_file: Failed to set attribute 0x{:02X} to \"{}\": {}\n",
                attrs,
                path.string(),
                ex.what());
        }
    }

    DosFileProperties properties;
    get_path_dos_properties(path, &properties, mode);
    return properties;
}


void resize_file(const std::filesystem::path & path, uint32_t new_size) {
#if defined(__cpp_lib_filesystem) && (__cpp_lib_filesystem >= 202002L)
    // Use C++23 std::filesystem::resize_file if available
    try {
        std::filesystem::resize_file(path, new_size);
        return;
    } catch (const std::filesystem::filesystem_error &) {
        throw FilesystemError(std::format("Cannot resize file: {}", strerror(errno)), DOS_EXTERR_ACCESS_DENIED);
    }
#else
    // Fallback to platform-specific implementation
#ifdef _WIN32
    FILE * const f = _wfopen(path.c_str(), L"r+b");
    if (!f) {
        throw FilesystemError(
            std::format("Cannot open file for resize: {}", strerror(errno)), DOS_EXTERR_ACCESS_DENIED);
    }
    const int fd = _fileno(f);
    const auto err = _chsize_s(fd, new_size);
    fclose(f);
    if (err != 0) {
        throw FilesystemError(std::format("Cannot resize file: {}", strerror(err)), DOS_EXTERR_ACCESS_DENIED);
    }
#else
    if (truncate(path.string().c_str(), new_size) != 0) {
        throw FilesystemError(std::format("Cannot resize file: {}", strerror(errno)), DOS_EXTERR_ACCESS_DENIED);
    }
#endif
#endif
}


void delete_file(const std::filesystem::path & file) {
    if (!std::filesystem::exists(file) && !std::filesystem::is_symlink(file)) {
        throw FilesystemError("delete_file: File does not exist: " + file.string(), DOS_EXTERR_FILE_NOT_FOUND);
    }
    if (std::filesystem::is_directory(file)) {
        throw FilesystemError("delete_file: Is a directory: " + file.string(), DOS_EXTERR_ACCESS_DENIED);
    }
    std::filesystem::remove(file);
}


void rename_file(const std::filesystem::path & old_name, const std::filesystem::path & new_name) {
    std::filesystem::rename(old_name, new_name);
}


std::pair<uint64_t, uint64_t> fs_space_info(const std::filesystem::path & path) {
    const auto info = std::filesystem::space(path);
    return {info.capacity, info.free};
}

}  // namespace

}  // namespace netmount_srv
