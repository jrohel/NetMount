// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#include "fs.hpp"

#include "logger.hpp"
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
uint8_t get_path_dos_properties(const std::filesystem::path & path, DosFileProperties * properties, bool use_fat_ioctl);

// Sets attributes `attrs` on file defined by `path`.
// Throws exception on error.
void set_item_attrs(const std::filesystem::path & path, uint8_t attrs);

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
DosFileProperties create_or_truncate_file(const std::filesystem::path & path, uint8_t attrs, bool use_fat_ioctl);

// Removes `file`
// Throws exception on error.
void delete_file(const std::filesystem::path & file);

// Renames `old_name` to `new_name`
// Throws exception on error or if no matching file found
void rename_file(const std::filesystem::path & old_name, const std::filesystem::path & new_name);

// Returns filesystem total size and free space in bytes, or 0, 0 on error
std::pair<uint64_t, uint64_t> fs_space_info(const std::filesystem::path & path);

// Returns `true` if `path` is on FAT filesystem
bool is_on_fat(const std::filesystem::path & path);

// Converts lowercase ascii characters to uppercase and removes illegal characters
// Returns new length and true if file name was shortened
std::pair<unsigned int, bool> sanitize_short_name(std::string_view in, char * out_buf, unsigned int buf_size);

// Converts server file name to DOS short name in FCB format
bool file_name_to_83(std::string_view long_name, fcb_file_name & fcb_name, std::set<fcb_file_name> & used_names);


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
    on_fat = ::netmount_srv::is_on_fat(this->root);
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
        throw std::runtime_error(
            std::format("Handle {} is invalid - only {} handles are currently allocated", handle, items.size()));
    }
    Item & item = items[handle];
    if (item.path.empty()) {
        throw std::runtime_error(std::format("Handle {} is invalid because it is empty", handle));
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
    long res;
    FILE * fd;
    auto & item = get_item(handle);
    const auto & fname = item.path;

    item.update_last_used_timestamp();

    fd = fopen(fname.string().c_str(), "rb");
    if (!fd) {
        throw std::runtime_error(std::format("Cannot open file: {}", strerror(errno)));
    }
    if (fseek(fd, offset, SEEK_SET) != 0) {
        auto orig_errno = errno;
        fclose(fd);
        throw std::runtime_error(std::format("Cannot seek in file: {}", strerror(orig_errno)));
    }
    res = fread(buffer, 1, len, fd);
    fclose(fd);

    return res;
}


int32_t Drive::write_file(const void * buffer, uint16_t handle, uint32_t offset, uint16_t len) {
    int32_t res;
    FILE * fd;
    auto & item = get_item(handle);
    const auto & fname = item.path;

    item.update_last_used_timestamp();

    // len 0 means "truncate" or "extend"
    if (len == 0) {
        log(LogLevel::DEBUG, "write_file: truncate \"{}\" to {} bytes\n", fname.string(), offset);
        if (truncate(fname.string().c_str(), offset) != 0) {
            throw std::runtime_error(std::format("Cannot truncate file: {}", strerror(errno)));
        }
        return 0;
    }

    //  write to file
    log(LogLevel::DEBUG, "write_file: write {} bytes into file \"{}\" at offset {}\n", len, fname.string(), offset);
    fd = fopen(fname.string().c_str(), "r+b");
    if (!fd) {
        throw std::runtime_error(std::format("Cannot open file: {}", strerror(errno)));
    }
    if (fseek(fd, offset, SEEK_SET) != 0) {
        auto orig_errno = errno;
        fclose(fd);
        throw std::runtime_error(std::format("Cannot seek in file: {}", strerror(orig_errno)));
    }
    res = fwrite(buffer, 1, len, fd);
    fclose(fd);

    return res;
}


int32_t Drive::get_file_size(uint16_t handle) {
    auto & item = get_item(handle);

    DosFileProperties fprops;
    if (get_path_dos_properties(item.path, &fprops, 0) == FAT_ERROR_ATTR) {
        return -1;
    }

    item.update_last_used_timestamp();

    return fprops.size;
}


bool Drive::find_file(
    uint16_t handle, const fcb_file_name & tmpl, unsigned char attr, DosFileProperties & properties, uint16_t & nth) {

    auto & item = get_item(handle);

    std::error_code ec;
    const bool is_root_dir = std::filesystem::equivalent(get_handle_path(handle), get_root(), ec);
    if (ec) {
        log(LogLevel::DEBUG, "find_file: {}\n", ec.message());
        return false;
    }

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

        // skip '.' and '..' items if directory is root
        if (is_root_dir && item_props.fcb_name.name_blank_padded[0] == '.')
            continue;
        if (!match_fcb_name_to_mask(tmpl, item_props.fcb_name))
            continue;

        if (attr == FAT_VOLUME) {
            // look only for VOLUME -> skip if not VOLUME
            if ((item_props.attrs & FAT_VOLUME) == 0) {
                continue;
            }
        } else {
            // return only file with at most the specified combination of hidden, system, and directory attributes
            if ((attr | (item_props.attrs & (FAT_HIDDEN | FAT_SYSTEM | FAT_DIRECTORY))) != attr)
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
        if (dir.fcb_name == fcb_name) {
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
        return {server_path, std::filesystem::exists(server_path)};
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
            throw std::runtime_error(
                std::format("create_server_path: Parent path not found: {}", (server_path / *prev_it).string()));
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
        throw std::runtime_error("make_dir: Directory exists: " + server_path.string());
    }
    netmount_srv::make_dir(server_path);

    // Recreates directory_list
    create_server_path(client_path, true);
}


void Drive::delete_dir(const std::filesystem::path & client_path) {
    auto [server_path, exist] = create_server_path(client_path);
    if (!exist) {
        throw std::runtime_error("delete_dir: Directory does not exist: " + server_path.string());
    }
    netmount_srv::delete_dir(server_path);

    // Recreates directory_list
    create_server_path(client_path, true);
}


void Drive::change_dir(const std::filesystem::path & client_path) {
    auto [server_path, exist] = create_server_path(client_path);
    if (!exist) {
        throw std::runtime_error("change_dir: Directory does not exist: " + server_path.string());
    }
    netmount_srv::change_dir(server_path);
}


void Drive::set_item_attrs(const std::filesystem::path & client_path, uint8_t attrs) {
    if (is_on_fat()) {
        auto [server_path, exist] = create_server_path(client_path);
        netmount_srv::set_item_attrs(server_path, attrs);

        // Recreates directory_list
        create_server_path(client_path, true);
    }
}


uint8_t Drive::get_dos_properties(const std::filesystem::path & client_path, DosFileProperties * properties) {
    auto [server_path, exist] = create_server_path(client_path);
    return get_server_path_dos_properties(server_path, properties);
}


uint8_t Drive::get_server_path_dos_properties(
    const std::filesystem::path & server_path, DosFileProperties * properties) {
    return get_path_dos_properties(server_path, properties, is_on_fat());
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

    if (get_path_dos_properties(server_path, NULL, is_on_fat()) & FAT_RO) {
        throw FilesystemError("Access denied: Read only FAT file system", DOS_EXTERR_ACCESS_DENIED);
    }

    if (exist) {
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
            try {
                netmount_srv::delete_file(path);
            } catch (const std::runtime_error & ex) {
                log(LogLevel::ERROR, "delete_files: Failed to delete file \"{}\": {}\n", path.string(), ex.what());
            }
        }
    }
}


DosFileProperties Drive::create_or_truncate_file(const std::filesystem::path & server_path, uint8_t attrs) {
    return netmount_srv::create_or_truncate_file(server_path, attrs, is_on_fat());
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
                for (const auto name : {".", ".."}) {
                    const auto fullpath = path / name;
                    DosFileProperties fprops;
                    get_path_dos_properties(fullpath, &fprops, drive.is_on_fat());
                    fprops.fcb_name = short_name_to_fcb(name);
                    if (drive.get_file_name_conversion() != Drive::FileNameConversion::OFF) {
                        fprops.server_name = name;
                    }
                    log(LogLevel::DEBUG,
                        "create_directory_list: {} -> {:.8s} {:.3s}\n",
                        name,
                        (char *)fprops.fcb_name.name_blank_padded,
                        (char *)fprops.fcb_name.ext_blank_padded);
                    directory_list.emplace_back(fprops);
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
            get_path_dos_properties(path, &fprops, drive.is_on_fat());
            if (drive.get_file_name_conversion() != Drive::FileNameConversion::OFF) {
                file_name_to_83(filename.string(), fprops.fcb_name, fcb_names);
                fprops.server_name = filename;
            }
            log(LogLevel::DEBUG,
                "create_directory_list: {} -> {:.8s} {:.3s}\n",
                filename.string(),
                (char *)fprops.fcb_name.name_blank_padded,
                (char *)fprops.fcb_name.ext_blank_padded);
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

    bool shortened = false;
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

        shortened = true;
    }

    // pad with spaces
    while (out_len < buf_size) {
        out_buf[--buf_size] = ' ';
    }

    return {out_len, shortened};
}


bool file_name_to_83(std::string_view long_name, fcb_file_name & fcb_name, std::set<fcb_file_name> & used_names) {
    const size_t dotPos = long_name.find_last_of('.');
    std::string_view base;
    std::string_view ext;
    if (dotPos != std::string::npos) {
        base = long_name.substr(0, dotPos);
        ext = long_name.substr(dotPos + 1);
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
    const std::filesystem::path & path, DosFileProperties * properties, [[maybe_unused]] bool use_fat_ioctl) {
    struct stat statbuf;
    if (stat(path.string().c_str(), &statbuf) != 0) {
        return FAT_ERROR_ATTR;  // error (probably doesn't exist)
    }

    if (properties) {
        // set file fcbname to the file part of path (ignore traling directory separators)
        auto it = path.end();
        while (it != path.begin() && (--it)->empty()) {
        }
        properties->fcb_name = short_name_to_fcb(it->string());

        properties->time_date = time_to_fat(statbuf.st_mtime);
    }

    if (S_ISDIR(statbuf.st_mode)) {
        if (properties) {
            properties->size = 0;
            properties->attrs = FAT_DIRECTORY;
        }
        return FAT_DIRECTORY;
    }

    // not a directory, set size
    if (properties) {
        properties->size = statbuf.st_size;
    }

#ifdef __linux__
    // if not a FAT drive, return a fake attribute archive
    if (!use_fat_ioctl) {
        if (properties) {
            properties->attrs = FAT_ARCHIVE;
        }
        return FAT_ARCHIVE;
    }

    // try to fetch DOS attributes from filesystem
    auto fd = open(path.string().c_str(), O_RDONLY);
    if (fd == -1) {
        return FAT_ERROR_ATTR;
    }
    uint32_t attr;
    if (ioctl(fd, FAT_IOCTL_GET_ATTRIBUTES, &attr) < 0) {
        log(LogLevel::ERROR, "get_path_dos_properties: Failed to fetch attributes of \"{}\"\n", path.string());
        close(fd);
        return 0;
    } else {
        close(fd);
        if (properties) {
            properties->attrs = attr;
        }
        return attr;
    }
#else
    if (properties) {
        properties->attrs = FAT_ARCHIVE;
    }
    return FAT_ARCHIVE;
#endif
}


void set_item_attrs([[maybe_unused]] const std::filesystem::path & path, [[maybe_unused]] uint8_t attrs) {
#ifdef __linux__
    int fd, res;
    fd = open(path.string().c_str(), O_RDONLY);
    if (fd == -1) {
        throw std::runtime_error(std::format("Cannot open file: {}", strerror(errno)));
    }
    res = ioctl(fd, FAT_IOCTL_SET_ATTRIBUTES, &attrs);
    auto orig_errno = errno;
    close(fd);
    if (res < 0) {
        throw std::runtime_error(std::format("Cannot set file attributes: {}", strerror(orig_errno)));
    }
#endif
}


void make_dir(const std::filesystem::path & dir) {
    if (!std::filesystem::create_directory(dir)) {
        throw std::runtime_error("make_dir: Directory exists: " + dir.string());
    }
}


void delete_dir(const std::filesystem::path & dir) {
    if (!std::filesystem::exists(dir)) {
        throw std::runtime_error("delete_dir: Directory does not exist: " + dir.string());
    }
    if (!std::filesystem::is_directory(dir)) {
        throw std::runtime_error("delete_dir: Not a directory: " + dir.string());
    }
    std::filesystem::remove(dir);
}


void change_dir(const std::filesystem::path & dir) { std::filesystem::current_path(dir); }


DosFileProperties create_or_truncate_file(const std::filesystem::path & path, uint8_t attrs, bool use_fat_ioctl) {
    // try to create/truncate the file
    FILE * const fd = fopen(path.string().c_str(), "wb");
    if (!fd) {
        throw std::runtime_error(std::format("Cannot open file: {}", strerror(errno)));
    }
    fclose(fd);

    // set FAT attributes
    if (use_fat_ioctl) {
        try {
            set_item_attrs(path, attrs);
        } catch (const std::runtime_error & ex) {
            log(LogLevel::ERROR,
                "create_or_truncate_file: Failed to set attribute 0x{:02X} to \"{}\": {}\n",
                attrs,
                path.string(),
                ex.what());
        }
    }

    DosFileProperties properties;
    get_path_dos_properties(path, &properties, use_fat_ioctl);
    return properties;
}


void delete_file(const std::filesystem::path & file) {
    if (!std::filesystem::exists(file)) {
        throw FilesystemError("delete_files: File does not exist: " + file.string(), DOS_EXTERR_FILE_NOT_FOUND);
    }
    if (std::filesystem::is_directory(file)) {
        throw FilesystemError("delete_files: Is a directory: " + file.string(), DOS_EXTERR_FILE_NOT_FOUND);
    }
    std::filesystem::remove(file);
}


void rename_file(const std::filesystem::path & old_name, const std::filesystem::path & new_name) {
    if (rename(old_name.string().c_str(), new_name.string().c_str()) != 0) {
        throw std::runtime_error("rename_file: Cannot rename " + old_name.string() + " to " + new_name.string());
    }
}


std::pair<uint64_t, uint64_t> fs_space_info(const std::filesystem::path & path) {
    const auto info = std::filesystem::space(path);
    return {info.capacity, info.free};
}


bool is_on_fat([[maybe_unused]] const std::filesystem::path & path) {
#ifdef __linux__
    auto fd = open(path.string().c_str(), O_RDONLY);
    if (fd == -1)
        return false;
    uint32_t volid;
    // try to get FAT volume id
    if (ioctl(fd, FAT_IOCTL_GET_VOLUME_ID, &volid) < 0) {
        close(fd);
        return false;
    }
    close(fd);
    return true;
#else
    return false;
#endif
}

}  // namespace

}  // namespace netmount_srv
