// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025-2026 Jaroslav Rohel, jaroslav.rohel@gmail.com

#ifndef _FS_HPP_
#define _FS_HPP_

#include "../shared/dos.h"
#include "config.hpp"

#include <stdint.h>
#include <string.h>

#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

// FAT attributes
#define FAT_NONE      0x00  // no attribute bits
#define FAT_RO        0x01
#define FAT_HIDDEN    0x02
#define FAT_SYSTEM    0x04
#define FAT_VOLUME    0x08
#define FAT_DIRECTORY 0x10
#define FAT_ARCHIVE   0x20
#define FAT_DEVICE    0x40

// Invalid attrtibutes, we use it to return error
#define FAT_ERROR_ATTR 0xFF

// Open modes
#define OPEN_MODE_RDONLY 0x00
#define OPEN_MODE_WRONLY 0x01
#define OPEN_MODE_RDWR   0x02

// Action code use low nibble for DOES exist file
#define IF_EXIST_MASK                0x0F
#define ACTION_CODE_FAIL_IF_EXIST    0x00
#define ACTION_CODE_OPEN_IF_EXIST    0x01
#define ACTION_CODE_REPLACE_IF_EXIST 0x02

// Action code use high nibble for does NOT exist file
#define IF_NOT_EXIST_MASK               0xF0
#define ACTION_CODE_FAIL_IF_NOT_EXIST   0x00
#define ACTION_CODE_CREATE_IF_NOT_EXIST 0x10


namespace netmount_srv {

enum class AttrsMode { AUTO, IGNORE, NATIVE, IN_EXTENDED };

class FilesystemError : public std::runtime_error {
public:
    FilesystemError(const std::string & msg, uint16_t dos_err_code) : runtime_error(msg), dos_err_code(dos_err_code) {}
    uint16_t get_dos_err_code() const noexcept { return dos_err_code; }

private:
    uint16_t dos_err_code;
};


struct DosFileProperties {
    fcb_file_name fcb_name;             // DOS FCB (file control block) style file name
    uint32_t size;                      // file size in bytes
    uint32_t time_date;                 // in DOS format
    uint32_t attrs;                     // DOS file/directory attributes
    std::filesystem::path server_name;  // File name on the server
};


class Drive {
public:
    enum class FileNameConversion { OFF, RAM };

    // Returns true if this drive is used (shared)
    bool is_shared() const noexcept { return used; }

    // Returns root path of shared drive.
    const std::filesystem::path & get_root() const noexcept { return root; }

    bool is_read_only() const noexcept { return read_only; }
    void set_read_only(bool read_only) noexcept { this->read_only = read_only; }

    void set_attrs_mode(AttrsMode mode) { attrs_mode = mode; }

    AttrsMode get_attrs_mode() const noexcept { return attrs_mode; }

    // Sets `root` for this drive. Initialize `used`.
    void set_root(std::filesystem::path root);

    void set_volume_label(const std::string & label);

    void set_file_name_conversion(FileNameConversion conversion) { name_conversion = conversion; }
    FileNameConversion get_file_name_conversion() const { return name_conversion; }

    Drive() = default;

    // Drive is accessed by reference. Make sure no one copies the Drive by mistake.
    Drive(const Drive &) = delete;
    Drive & operator=(const Drive &) = delete;

    /// Returns the handle (start cluster in dos) of a filesystem item (file or directory).
    /// Returns 0xffff on error
    uint16_t get_handle(const std::filesystem::path & server_path);

    /// Returns the path to the filesystem item represented by the handle.
    const std::filesystem::path & get_handle_path(uint16_t handle);

    /// Reads `len` bytes from `offset` from the file defined by `handle` to `buffer`.
    /// Returns the number of bytes read
    /// Throws exception on error
    int32_t read_file(void * buffer, uint16_t handle, uint32_t offset, uint16_t len);

    /// Writes `len` bytes from `buffer` to the file defined by `handle` starting at position `offset`.
    /// Returns the number of bytes written.
    /// Throws exception on error
    int32_t write_file(const void * buffer, uint16_t handle, uint32_t offset, uint16_t len);

    /// Returns the size of file defined by handle (or -1 on error)
    int32_t get_file_size(uint16_t handle);

    /// Searches for files matching template `tmpl` in directory defined by `handle`
    /// with at most attributes `attr`.
    /// Fills in `properties` with the next match after `nth` and updates `nth`
    /// Returns `true` on success.
    bool find_file(
        uint16_t handle,
        const fcb_file_name & tmpl,
        unsigned char attr,
        DosFileProperties & properties,
        uint16_t & nth);

    /// Appends the path from the client to the `root` of the shared drive.
    /// The `client_path` is transformed to an actual existing path on the server. If the last part of the path
    /// (filename) is not found on the server, it uses the name from `client_path` and sets the `bool` value to `false`.
    /// Throws exception on error.
    std::pair<std::filesystem::path, bool> create_server_path(
        const std::filesystem::path & client_path, bool create_directory_list = false);

    /// Throws exception on error.
    void make_dir(const std::filesystem::path & client_path);

    /// Throws exception on error.
    void delete_dir(const std::filesystem::path & client_path);

    /// Throws exception on error.
    void change_dir(const std::filesystem::path & client_path);

    /// Sets attributes `attrs` on file defined by `client_path`.
    /// Throws exception on error.
    void set_item_attrs(const std::filesystem::path & client_path, uint8_t attrs);

    /// Gets attributes of file defined by `server_path`.
    /// Throws exception on error.
    uint8_t get_server_path_attrs(const std::filesystem::path & server_path);

    /// Fills the DosFileProperties structure if `properties` != nullptr.
    /// Returns DOS attributes for `client_path` or FAT_ERROR_ATTR on error.
    /// DOS attr flags: 1=RO 2=HID 4=SYS 8=VOL 16=DIR 32=ARCH 64=DEVICE
    uint8_t get_dos_properties(const std::filesystem::path & client_path, DosFileProperties * properties);

    uint8_t get_server_path_dos_properties(const std::filesystem::path & server_path, DosFileProperties * properties);

    /// Renames `old_client_name` to `new_client_name`
    void rename_file(const std::filesystem::path & old_client_path, const std::filesystem::path & new_client_path);

    /// Removes all files matching the pattern
    /// Throws exception on error or if no matching file found
    void delete_files(const std::filesystem::path & client_pattern);

    /// Creates or truncates a file `server_path` with attributes `attrs`.
    /// Returns properties of created/truncated file.
    /// Throws exception on error.
    DosFileProperties create_or_truncate_file(const std::filesystem::path & server_path, uint8_t attrs);

    /// Try to open a file, then close it.
    void try_open_file(const std::filesystem::path & server_path, uint8_t open_mode);

    /// Returns filesystem total size and free space in bytes, or 0, 0 on error
    std::pair<uint64_t, uint64_t> space_info();

private:
    constexpr static uint16_t MAX_HANDLE_COUNT = 0xFFFFU;

    bool used{false};
    std::filesystem::path root;
    bool read_only{false};
    fcb_file_name volume_label;
    bool has_volume_label{false};
    AttrsMode attrs_mode{AttrsMode::AUTO};
    FileNameConversion name_conversion{FileNameConversion::RAM};

    class Item {
    public:
        std::filesystem::path path;                     // path to filesystem item
        time_t last_used_time;                          // when this item was last used
        std::vector<DosFileProperties> directory_list;  // used by FIND_FIRST and FIND_NEXT
        std::set<fcb_file_name> fcb_names;

        // Creates a directory listing for `path`.
        // Returns the number of filesystem entries, or -1 if an error occurs.
        int32_t create_directory_list(const Drive & drive);

        void update_last_used_timestamp();
    };
    std::vector<Item> items;

    Item & get_item(uint16_t handle);

    const std::filesystem::path & get_server_name(
        uint16_t handle, const fcb_file_name & fcb_name, bool create_directory_list);
};


// convert short file name to fcb_file_name structure
fcb_file_name short_name_to_fcb(const std::string & short_name) noexcept;

#if DOS_ATTRS_NATIVE == 1
bool is_dos_attrs_native_supported(const std::filesystem::path & path);
uint8_t get_dos_attrs_native(const std::filesystem::path & path);
void set_dos_attrs_native(const std::filesystem::path & path, uint8_t attrs);
#endif

#if DOS_ATTRS_IN_EXTENDED == 1
bool is_dos_attrs_in_extended_supported(const std::filesystem::path & path);
uint8_t get_dos_attrs_from_extended(const std::filesystem::path & path);
void set_dos_attrs_to_extended(const std::filesystem::path & path, uint8_t attrs);
#endif

}  // namespace netmount_srv

#endif
