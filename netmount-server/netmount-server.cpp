// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#include "../shared/dos.h"
#include "../shared/drvproto.h"
#include "fs.hpp"
#include "logger.hpp"
#include "slip_udp_serial.hpp"
#include "udp_socket.hpp"
#include "utils.hpp"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#define PROGRAM_VERSION "1.3.0"

// structs are packed
#pragma pack(1)

namespace netmount_srv {

namespace {

// Reply cache - contains the last replies sent to clients
// It is used in case a client has not received reply and resends request so that we don't process
// the request again (which can be dangerous in case of write requests).
constexpr int REPLY_CACHE_SIZE = 16;
class ReplyCache {
public:
    struct ReplyInfo {
        std::array<uint8_t, 1500> packet;  // entire packet that was sent
        uint16_t len{0};                   // packet length
        uint32_t ipv4_addr;                // remote IP address
        uint16_t udp_port;                 // remote UDP port
        time_t timestamp;                  // time of answer (so if cache full I can drop oldest)

        ReplyInfo() = default;

        // ReplyInfo is accessed by reference. Make sure no one copies the ReplyInfo by mistake.
        ReplyInfo(const ReplyInfo &) = delete;
        ReplyInfo & operator=(const ReplyInfo &) = delete;
    };

    // Finds the cache entry related to given client, or the oldest one for reuse
    ReplyInfo & get_reply_info(uint32_t ipv4_addr, uint16_t udp_port) noexcept;

private:
    std::array<ReplyInfo, REPLY_CACHE_SIZE> items;
};


ReplyCache::ReplyInfo & ReplyCache::get_reply_info(uint32_t ipv4_addr, uint16_t udp_port) noexcept {
    auto * oldest_item = &items[0];

    // search for item with matching address (ip and port)
    for (auto & item : items) {
        if (item.ipv4_addr == ipv4_addr && item.udp_port == udp_port) {
            return item;  // found
        }
        if (item.timestamp < oldest_item->timestamp) {
            oldest_item = &item;
        }
    }

    // matching item not found, reuse oldest item
    oldest_item->len = 0;  // invalidate old content by setting length to 0
    oldest_item->ipv4_addr = ipv4_addr;
    oldest_item->udp_port = udp_port;
    return *oldest_item;
}


// Define global data
ReplyCache answer_cache;

constexpr size_t MAX_DRIVES_COUNT = 'Z' - 'A' + 1;
std::array<Drive, MAX_DRIVES_COUNT> drives;

UdpSocket * udp_socket_ptr{nullptr};

// the flag is set when netmount-server is expected to terminate
sig_atomic_t volatile exit_flag = 0;


void signal_handler(int sig_number) {
    switch (sig_number) {
        case SIGINT:
#ifdef SIGQUIT
        case SIGQUIT:
#endif
        case SIGTERM:
            exit_flag = 1;
            if (udp_socket_ptr) {
                udp_socket_ptr->signal_stop();
            }
            break;
        default:
            break;
    }
}


// Returns a FCB file name as C string (with added null terminator), this is used only by debug routines
//#ifdef DEBUG
char * fcb_file_name_to_cstr(const fcb_file_name & s) {
    static char name_cstr[sizeof(fcb_file_name) + 1] = {'\0'};
    memcpy(name_cstr, &s, sizeof(fcb_file_name));
    return name_cstr;
}
//#endif


// Creates a relative path from the value in buff
std::filesystem::path create_relative_path(const void * buff, uint16_t len) {
    auto * ptr = reinterpret_cast<const char *>(buff);

    std::string search_template(ptr, len);
    std::transform(search_template.begin(), search_template.end(), search_template.begin(), ascii_to_lower);
    std::replace(search_template.begin(), search_template.end(), '\\', '/');
    return std::filesystem::path(search_template).relative_path();
}


// Processes client requests and prepares responses.
int process_request(ReplyCache::ReplyInfo & reply_info, const uint8_t * request_packet, int request_packet_len) {

    // must contain at least the header
    if (request_packet_len < static_cast<int>(sizeof(struct drive_proto_hdr))) {
        return -1;
    }

    auto const * const request_header = reinterpret_cast<struct drive_proto_hdr const *>(request_packet);
    auto * const reply_header = reinterpret_cast<struct drive_proto_hdr *>(reply_info.packet.data());

    // ReplyCache contains a packet (length > 0) with the same sequence number, re-send it.
    if (reply_info.len > 0 && reply_header->sequence == request_header->sequence) {
        log(LogLevel::NOTICE, "Using a packet from the reply cache (seq {:d})\n", reply_header->sequence);
        return reply_info.len;
    }

    *reply_header = *request_header;

    auto const * const request_data = reinterpret_cast<const uint8_t *>(request_header + 1);
    auto * const reply_data = reinterpret_cast<uint8_t *>(reply_header + 1);
    const uint16_t request_data_len = request_packet_len - sizeof(struct drive_proto_hdr);

    const unsigned int reqdrv = request_header->drive & 0x1F;
    const int function = request_header->function;
    uint16_t * const ax = &reply_header->ax;
    int reply_packet_len = 0;

    if ((reqdrv < 2) || (reqdrv >= drives.size())) {
        log(LogLevel::ERROR, "Requested invalid drive number: {:d}\n", reqdrv);
        return -1;
    }

    // Do I share this drive?
    auto & drive = drives[reqdrv];
    if (!drive.is_shared()) {
        log(LogLevel::WARNING, "Requested drive is not shared: {:c}: (number {:d})\n", 'A' + reqdrv, reqdrv);
        return -1;
    }

    // assume success
    *ax = to_little16(DOS_EXTERR_NO_ERROR);

    log(LogLevel::TRACE,
        "Got query: 0x{:02X} [{:02X} {:02X} {:02X} {:02X}]\n",
        function,
        request_data[0],
        request_data[1],
        request_data[2],
        request_data[3]);

    switch (function) {
        case INT2F_REMOVE_DIR:
        case INT2F_MAKE_DIR: {
            if (request_data_len < 1) {
                return -1;
            }
            const auto relative_path = create_relative_path(request_data, request_data_len);

            if (function == INT2F_MAKE_DIR) {
                log(LogLevel::DEBUG, "MAKE_DIR \"{:c}:\\{}\"\n", reqdrv + 'A', relative_path.string());
                try {
                    drive.make_dir(relative_path);
                } catch (const std::runtime_error & ex) {
                    *ax = to_little16(DOS_EXTERR_WRITE_FAULT);
                    log(LogLevel::WARNING,
                        "MAKE_DIR \"{:c}:\\{}\": {}\n",
                        reqdrv + 'A',
                        relative_path.string(),
                        ex.what());
                }
            } else {
                log(LogLevel::DEBUG, "REMOVE_DIR \"{:c}:\\{}\"\n", reqdrv + 'A', relative_path.string());
                try {
                    drive.delete_dir(relative_path);
                } catch (const std::runtime_error & ex) {
                    *ax = to_little16(DOS_EXTERR_WRITE_FAULT);
                    log(LogLevel::WARNING,
                        "REMOVE_DIR \"{:c}:\\{}\": {}\n",
                        reqdrv + 'A',
                        relative_path.string(),
                        ex.what());
                }
            }
        } break;

        case INT2F_CHANGE_DIR: {
            if (request_data_len < 1) {
                return -1;
            }
            const auto relative_path = create_relative_path(request_data, request_data_len);

            log(LogLevel::DEBUG, "CHANGE_DIR \"{:c}:\\{}\"\n", reqdrv + 'A', relative_path.string());
            // Try to chdir to this dir
            try {
                drive.change_dir(relative_path);
            } catch (const std::runtime_error & ex) {
                log(LogLevel::WARNING,
                    "REMOVE_DIR \"{:c}:\\{}\": {}\n",
                    reqdrv + 'A',
                    relative_path.string(),
                    ex.what());
                *ax = to_little16(DOS_EXTERR_PATH_NOT_FOUND);
            }
            break;
        }

        case INT2F_CLOSE_FILE: {
            if (request_data_len != sizeof(drive_proto_closef)) {
                return -1;
            }
            // Only checking the existence of the handle because I don't keep files open.
            auto * const request = reinterpret_cast<const drive_proto_closef *>(request_data);
            const uint16_t handle = from_little16(request->start_cluster);
            log(LogLevel::DEBUG, "CLOSE_FILE handle {}\n", handle);
            try {
                drive.get_handle_path(handle);
            } catch (const std::runtime_error & ex) {
                log(LogLevel::WARNING, "CLOSE_FILE handle {}: {}\n", handle, ex.what());
                // TODO: Send error to client?
            }
        } break;

        case INT2F_READ_FILE: {
            if (request_data_len != sizeof(drive_proto_readf)) {
                return -1;
            }
            auto * const request = reinterpret_cast<const drive_proto_readf *>(request_data);
            const uint32_t offset = from_little32(request->offset);
            const uint16_t handle = from_little16(request->start_cluster);
            const uint16_t len = from_little16(request->length);
            log(LogLevel::DEBUG, "READ_FILE handle {}, {} bytes, offset {}\n", handle, len, offset);
            try {
                reply_packet_len = drive.read_file(reply_data, handle, offset, len);
            } catch (const std::runtime_error & ex) {
                log(LogLevel::WARNING, "READ_FILE handle {}: {}\n", handle, ex.what());
                *ax = to_little16(DOS_EXTERR_ACCESS_DENIED);
            }
        } break;

        case INT2F_WRITE_FILE: {
            if (request_data_len < sizeof(drive_proto_writef)) {
                return -1;
            }
            auto * const request = reinterpret_cast<const drive_proto_writef *>(request_data);
            const uint32_t offset = from_little32(request->offset);
            const uint16_t handle = from_little16(request->start_cluster);
            log(LogLevel::DEBUG,
                "WRITE_FILE handle {}, {} bytes, offset {}\n",
                handle,
                request_data_len - sizeof(drive_proto_writef),
                offset);
            try {
                const auto write_len = drive.write_file(
                    request_data + sizeof(drive_proto_writef),
                    handle,
                    offset,
                    request_data_len - sizeof(drive_proto_writef));
                auto * const reply = reinterpret_cast<drive_proto_writef_reply *>(reply_data);
                reply->written = to_little16(write_len);
                reply_packet_len = sizeof(drive_proto_writef_reply);
            } catch (const std::runtime_error & ex) {
                log(LogLevel::WARNING, "WRITE_FILE handle {}: {}\n", handle, ex.what());
                *ax = to_little16(DOS_EXTERR_ACCESS_DENIED);
            }

        } break;

        case INT2F_LOCK_UNLOCK_FILE: {
            if (request_data_len < sizeof(drive_proto_lockf)) {
                return -1;
            }
            // Only checking the existence of the handle
            // TODO: Try to lock file?
            auto * const request = reinterpret_cast<const drive_proto_lockf *>(request_data);
            const uint16_t handle = from_little16(request->start_cluster);
            log(LogLevel::DEBUG, "LOCK_UNLOCK_FILE handle {}\n", handle);
            try {
                drive.get_handle_path(handle);
            } catch (const std::runtime_error & ex) {
                log(LogLevel::ERROR, "LOCK_UNLOCK_FILE handle {}: {}\n", handle, ex.what());
                // TODO: Send error to client?
            }
        } break;

        case INT2F_UNLOCK_FILE: {
            if (request_data_len < sizeof(drive_proto_lockf)) {
                return -1;
            }
            // Only checking the existence of the handle
            // TODO: Implement unlock after lock.
            auto * const request = reinterpret_cast<const drive_proto_lockf *>(request_data);
            const uint16_t handle = from_little16(request->start_cluster);
            log(LogLevel::DEBUG, "UNLOCK_FILE handle {}\n", handle);
            try {
                drive.get_handle_path(handle);
            } catch (const std::runtime_error & ex) {
                log(LogLevel::ERROR, "UNLOCK_FILE handle {}: {}\n", handle, ex.what());
                // TODO: Send error to client?
            }
        } break;

        case INT2F_DISK_INFO: {
            log(LogLevel::DEBUG, "DISK_INFO for drive {:c}:\n", 'A' + reqdrv);
            try {
                auto [fs_size, free_space] = drive.space_info();
                // limit results to slightly under 2 GiB (otherwise MS-DOS is confused)
                if (fs_size >= 2lu * 1024 * 1024 * 1024)
                    fs_size = 2lu * 1024 * 1024 * 1024 - 1;
                if (free_space >= 2lu * 1024 * 1024 * 1024)
                    free_space = 2lu * 1024 * 1024 * 1024 - 1;
                log(LogLevel::DEBUG, "  TOTAL: {} KiB ; FREE: {} KiB\n", fs_size >> 10, free_space >> 10);
                // AX: media id (8 bits) | sectors per cluster (8 bits)
                // etherdfs says: MSDOS tolerates only 1 here!
                *ax = to_little16(1);
                auto * const reply = reinterpret_cast<drive_proto_disk_info_reply *>(reply_data);
                reply->total_clusters = to_little16(fs_size >> 15);  // 32K clusters
                reply->bytes_per_sector = to_little16(32768);
                reply->available_clusters = to_little16(free_space >> 15);  // 32K clusters
                reply_packet_len = sizeof(drive_proto_disk_info_reply);
            } catch (const std::runtime_error & ex) {
                log(LogLevel::WARNING, "DISK_INFO: for drive {:c}: {}\n", 'A' + reqdrv, ex.what());
                return -1;
            }
        } break;

        case INT2F_SET_ATTRS: {
            if (request_data_len <= sizeof(drive_proto_set_attrs)) {
                return -1;
            }
            auto * const request = reinterpret_cast<const drive_proto_set_attrs *>(request_data);
            unsigned char attrs = request->attrs;
            const auto relative_path = create_relative_path(request_data + 1, request_data_len - 1);

            log(LogLevel::DEBUG,
                "SET_ATTRS file \"{:c}:\\{}\", attr: 0x{:02X}\n",
                reqdrv + 'A',
                relative_path.string(),
                attrs);
            try {
                drive.set_item_attrs(relative_path, attrs);
            } catch (const std::runtime_error & ex) {
                log(LogLevel::WARNING,
                    "SET_ATTRS failed to set 0x{:02X} to \"{:c}:\\{}\": {}\n",
                    attrs,
                    reqdrv + 'A',
                    relative_path.string(),
                    ex.what());
                *ax = to_little16(DOS_EXTERR_FILE_NOT_FOUND);
            }
        } break;

        case INT2F_GET_ATTRS: {
            if (request_data_len < 1) {
                return -1;
            }
            const auto relative_path = create_relative_path(request_data, request_data_len);

            log(LogLevel::DEBUG, "GET_ATTRS file \"{:c}:\\{}\"\n", reqdrv + 'A', relative_path.string());
            DosFileProperties properties;
            uint8_t attrs;
            try {
                attrs = drive.get_dos_properties(relative_path, &properties);
            } catch (const std::runtime_error &) {
                attrs = FAT_ERROR_ATTR;
            }
            if (attrs == FAT_ERROR_ATTR) {
                log(LogLevel::NOTICE, "GET_ATTRS file not found \"{:c}:\\{}\"\n", reqdrv + 'A', relative_path.string());
                *ax = to_little16(DOS_EXTERR_FILE_NOT_FOUND);
            } else {
                log(LogLevel::DEBUG,
                    "GET_ATTRS \"{:c}:\\{}\" size {} bytes, attr 0x{:02X}\n",
                    reqdrv + 'A',
                    relative_path.string(),
                    properties.size,
                    properties.attrs);
                auto * const reply = reinterpret_cast<drive_proto_get_attrs_reply *>(reply_data);
                reply->time = to_little16(properties.time_date);
                reply->date = to_little16(properties.time_date >> 16);
                reply->size_lo = to_little16(properties.size);
                reply->size_hi = to_little16(properties.size >> 16);
                reply->attrs = properties.attrs;
                reply_packet_len = sizeof(drive_proto_get_attrs_reply);
            }
        } break;

        case INT2F_RENAME_FILE: {
            // At least 3 bytes, expected two paths, one is zero terminated
            if (request_data_len < 3) {
                return -1;
            }
            const int path1_len = request_data[0];
            const int path2_len = request_data_len - (1 + path1_len);
            if (request_data_len > path1_len) {
                const auto old_relative_path = create_relative_path(request_data + 1, path1_len);
                const auto new_relative_path = create_relative_path(request_data + 1 + path1_len, path2_len);

                log(LogLevel::DEBUG,
                    "RENAME_FILE: \"{:c}:\\{}\" -> \"{:c}:\\{}\"\n",
                    reqdrv + 'A',
                    old_relative_path.string(),
                    reqdrv + 'A',
                    new_relative_path.string());

                try {
                    drive.rename_file(old_relative_path, new_relative_path);
                } catch (const std::runtime_error & ex) {
                    log(LogLevel::WARNING,
                        "RENAME_FILE: \"{:c}:\\{}\" -> \"{:c}:\\{}\": {}\n",
                        reqdrv + 'A',
                        old_relative_path.string(),
                        reqdrv + 'A',
                        new_relative_path.string(),
                        ex.what());
                    *ax = to_little16(DOS_EXTERR_ACCESS_DENIED);
                }
            } else {
                *ax = to_little16(DOS_EXTERR_FILE_NOT_FOUND);
            }
        } break;

        case INT2F_DELETE_FILE: {
            if (request_data_len < 1) {
                return -1;
            }
            const auto relative_path = create_relative_path(request_data, request_data_len);
            log(LogLevel::DEBUG, "DELETE_FILE \"{:c}:\\{}\"\n", reqdrv + 'A', relative_path.string());
            try {
                drive.delete_files(relative_path);
            } catch (const FilesystemError & ex) {
                log(LogLevel::WARNING,
                    "DELETE_FILE \"{:c}:\\{}\": {}\n",
                    reqdrv + 'A',
                    relative_path.string(),
                    ex.what());
                *ax = to_little16(ex.get_dos_err_code());
            }
        } break;

        case INT2F_FIND_FIRST: {
            if (request_data_len <= sizeof(drive_proto_find_first)) {
                return -1;
            }
            auto * const request = reinterpret_cast<const drive_proto_find_first *>(request_data);
            const uint8_t fattr = request->attrs;
            const auto search_template = create_relative_path(request_data + 1, request_data_len - 1);
            const auto search_template_parent = search_template.parent_path();
            const std::string filemask = search_template.filename().string();

            log(LogLevel::DEBUG,
                "FIND_FIRST in \"{:c}:\\{}\"\n filemask: \"{}\"\n attrs: 0x{:2X}\n",
                reqdrv + 'A',
                search_template_parent.string(),
                filemask,
                fattr);

            const auto filemaskfcb = short_name_to_fcb(filemask);

            uint16_t handle;
            try {
                const auto [server_directory, exist] = drive.create_server_path(search_template_parent);
                if (!exist) {
                    log(LogLevel::NOTICE, "FIND_FIRST Directory does not exist: {}\n", search_template_parent.string());
                    // do not use DOS_EXTERR_FILE_NOT_FOUND, some applications rely on a failing FIND_FIRST
                    // to return DOS_EXTERR_NO_MORE_FILES (e.g. LapLink 5)
                    *ax = to_little16(DOS_EXTERR_NO_MORE_FILES);
                    break;
                }
                handle = drive.get_handle(server_directory);
            } catch (const std::runtime_error &) {
                handle = 0xFFFFU;
            }
            DosFileProperties properties;
            uint16_t fpos = 0;
            if ((handle == 0xFFFFU) || !drive.find_file(handle, filemaskfcb, fattr, properties, fpos)) {
                log(LogLevel::INFO,
                    "FIND_FIRST No matching file found in \"{:c}:\\{}\"\n filemask: \"{}\"\n attrs: 0x{:2X}\n",
                    reqdrv + 'A',
                    search_template_parent.string(),
                    filemask,
                    fattr);

                // do not use DOS_EXTERR_FILE_NOT_FOUND, some applications rely on a failing FIND_FIRST
                // to return DOS_EXTERR_NO_MORE_FILES (e.g. LapLink 5)
                *ax = to_little16(DOS_EXTERR_NO_MORE_FILES);
            } else {
                log(LogLevel::DEBUG,
                    "FIND_FIRST Found file: FCB \"{}\", attrs 0x{:02X}\n",
                    fcb_file_name_to_cstr(properties.fcb_name),
                    properties.attrs);
                auto * const reply = reinterpret_cast<drive_proto_find_reply *>(reply_data);
                reply->attrs = properties.attrs;
                reply->name = properties.fcb_name;
                reply->time = to_little16(properties.time_date);
                reply->date = to_little16(properties.time_date >> 16);
                reply->size = to_little32(properties.size);
                reply->start_cluster = to_little16(handle);
                reply->dir_entry = to_little16(fpos);
                reply_packet_len = sizeof(drive_proto_find_reply);
            }
        } break;

        case INT2F_FIND_NEXT: {
            if (request_data_len != sizeof(drive_proto_find_next)) {
                return -1;
            }
            auto * const request = reinterpret_cast<const drive_proto_find_next *>(request_data);
            const uint16_t handle = from_little16(request->cluster);
            uint16_t fpos = from_little16(request->dir_entry);
            const uint8_t fattr = request->attrs;
            fcb_file_name const * const fcbmask = &request->search_template;
            log(LogLevel::DEBUG,
                "FIND_NEXT looks for {} file in dir handle {}\n fcbmask: \"{}\"\n attrs: 0x{:2X}\n",
                fpos,
                handle,
                fcb_file_name_to_cstr(*fcbmask),
                fattr);
            try {
                DosFileProperties properties;
                if (!drive.find_file(handle, *fcbmask, fattr, properties, fpos)) {
                    log(LogLevel::DEBUG, "No more matching files found\n");
                    *ax = to_little16(DOS_EXTERR_NO_MORE_FILES);
                } else {
                    log(LogLevel::DEBUG,
                        "Found file: FCB \"{}\", attrs 0x{:02X}\n",
                        fcb_file_name_to_cstr(properties.fcb_name),
                        properties.attrs);
                    auto * const reply = reinterpret_cast<drive_proto_find_reply *>(reply_data);
                    reply->attrs = properties.attrs;
                    reply->name = properties.fcb_name;
                    reply->time = to_little16(properties.time_date);
                    reply->date = to_little16(properties.time_date >> 16);
                    reply->size = to_little32(properties.size);
                    reply->start_cluster = to_little16(handle);
                    reply->dir_entry = to_little16(fpos);
                    reply_packet_len = sizeof(drive_proto_find_reply);
                }
            } catch (const std::runtime_error & ex) {
                log(LogLevel::WARNING,
                    "FIND_NEXT failed looking for {} file in dir handle {}\n fcbmask: \"{}\"\n attrs: 0x{:2X}\n",
                    fpos,
                    handle,
                    fcb_file_name_to_cstr(*fcbmask),
                    fattr);
                *ax = to_little16(DOS_EXTERR_NO_MORE_FILES);
            }
        } break;

        case INT2F_SEEK_FROM_END: {
            if (request_data_len != sizeof(drive_proto_seek_from_end)) {
                return -1;
            }
            auto * const request = reinterpret_cast<const drive_proto_seek_from_end *>(request_data);
            // translate a "seek from end" offset into an "seek from start" offset
            int32_t offset = from_little16(request->offset_from_end_hi);
            offset = (offset << 16) + from_little16(request->offset_from_end_lo);
            const uint16_t handle = from_little16(request->start_cluster);

            int32_t fsize;
            try {
                log(LogLevel::DEBUG, "SEEK_FROM_END on file handle {}, offset {}\n", handle, offset);
                // if the offset is positive, zero it
                if (offset > 0) {
                    offset = 0;
                }
                fsize = drive.get_file_size(handle);
            } catch (const std::runtime_error &) {
                fsize = -1;
            }
            if (fsize < 0) {
                log(LogLevel::WARNING, "SEEK_FROM_END file not found or other error\n");
                *ax = to_little16(DOS_EXTERR_FILE_NOT_FOUND);
            } else {
                // compute new offset and send it back
                offset += fsize;
                if (offset < 0) {
                    offset = 0;
                }
                log(LogLevel::DEBUG,
                    "SEEK_FROM_END File handle {}, size {} bytes, new offset {}\n",
                    handle,
                    fsize,
                    offset);
                auto * const reply = reinterpret_cast<drive_proto_seek_from_end_reply *>(reply_data);
                reply->position_lo = to_little16(offset);
                reply->position_hi = to_little16(offset >> 16);
                reply_packet_len = sizeof(drive_proto_seek_from_end_reply);
            }
        } break;

        case INT2F_OPEN_FILE:
        case INT2F_CREATE_FILE:
        case INT2F_EXTENDED_OPEN_CREATE_FILE: {
            if (request_data_len <= sizeof(drive_proto_open_create)) {
                return -1;
            }
            // OPEN is only about "does this file exist", and CREATE "create or truncate this file",
            // EXTENDED_OPEN_CREATE is a combination of both with extra flags
            auto * const request = reinterpret_cast<const drive_proto_open_create *>(request_data);
            const uint16_t stack_attr = from_little16(request->attrs);
            const uint16_t action_code = from_little16(request->action);
            const uint16_t ext_open_create_open_mode = from_little16(request->mode);

            try {
                const auto relative_path = create_relative_path(request_data + 6, request_data_len - 6);
                const auto [server_path, exist] = drive.create_server_path(relative_path);
                const auto server_directory = server_path.parent_path();

                log(LogLevel::DEBUG,
                    "OPEN/CREATE/EXTENDED_OPEN_CREATE \"{:c}:\\{}\", stack_attr=0x{:04X}\n",
                    reqdrv + 'A',
                    relative_path.string(),
                    stack_attr);
                std::error_code ec;
                if (!std::filesystem::is_directory(server_directory)) {
                    log(LogLevel::WARNING,
                        "OPEN/CREATE/EXTENDED_OPEN_CREATE: Directory \"{}\" does not exist\n",
                        server_directory.string());
                    *ax = to_little16(DOS_EXTERR_PATH_NOT_FOUND);
                } else {
                    bool error = false;
                    uint8_t result_open_mode;
                    uint16_t ext_open_create_result_code = 0;
                    DosFileProperties properties;

                    if (function == INT2F_OPEN_FILE) {
                        log(LogLevel::DEBUG,
                            "OPEN_FILE \"{}\", stack_attr=0x{:04X}\n",
                            server_path.string(),
                            stack_attr);
                        result_open_mode = stack_attr & 0xFF;
                        // check that item exists, and is neither a volume nor a directory
                        const auto attr = drive.get_server_path_dos_properties(server_path, &properties);
                        if (attr == 0xFF || ((attr & (FAT_VOLUME | FAT_DIRECTORY)) != 0)) {
                            error = true;
                        }
                    } else if (function == INT2F_CREATE_FILE) {
                        log(LogLevel::DEBUG,
                            "CREATE_FILE \"{}\", stack_attr=0x{:04X}\n",
                            server_path.string(),
                            stack_attr);
                        properties = drive.create_or_truncate_file(server_path, stack_attr & 0xFF);
                        result_open_mode = 2;  // read/write
                    } else {
                        log(LogLevel::DEBUG,
                            "EXTENDED_OPEN_CREATE_FILE \"{}\", stack_attr=0x{:04X}, action_code=0x{:04X}, "
                            "open_mode=0x{:04X}\n",
                            server_path.string(),
                            stack_attr,
                            action_code,
                            ext_open_create_open_mode);

                        const auto attr = drive.get_server_path_dos_properties(server_path, &properties);
                        result_open_mode =
                            ext_open_create_open_mode & 0x7f;  // etherdfs says: that's what PHANTOM.C does
                        if (attr == FAT_ERROR_ATTR) {          // file not found
                            log(LogLevel::DEBUG, "File doesn't exist -> ");
                            if ((action_code & IF_NOT_EXIST_MASK) == ACTION_CODE_CREATE_IF_NOT_EXIST) {
                                log(LogLevel::DEBUG, "create file\n");
                                properties = drive.create_or_truncate_file(server_path, stack_attr & 0xFF);
                                ext_open_create_result_code = DOS_EXT_OPEN_FILE_RESULT_CODE_CREATED;
                            } else {
                                log(LogLevel::WARNING,
                                    "EXTENDED_OPEN_CREATE_FILE fail: file \"{}\" does not exist\n",
                                    server_path.string());
                                error = true;
                            }
                        } else if ((attr & (FAT_VOLUME | FAT_DIRECTORY)) != 0) {
                            log(LogLevel::WARNING,
                                "OPEN/CREATE/EXTENDED_OPEN_CREATE Item \"{}\" is either a DIR or a VOL\n",
                                server_path.string());
                            error = true;
                        } else {
                            log(LogLevel::DEBUG, "File exists already (attr 0x{:02X}) -> ", attr);
                            if ((action_code & IF_EXIST_MASK) == ACTION_CODE_OPEN_IF_EXIST) {
                                log(LogLevel::DEBUG, "open file\n");
                                ext_open_create_result_code = DOS_EXT_OPEN_FILE_RESULT_CODE_OPENED;
                            } else if ((action_code & IF_EXIST_MASK) == ACTION_CODE_REPLACE_IF_EXIST) {
                                log(LogLevel::DEBUG, "truncate file\n");
                                properties = drive.create_or_truncate_file(server_path, stack_attr & 0xFF);
                                ext_open_create_result_code = DOS_EXT_OPEN_FILE_RESULT_CODE_TRUNCATED;
                            } else {
                                log(LogLevel::WARNING, "OPEN/CREATE/EXTENDED_OPEN_CREATE Fail, file already exists\n");
                                error = true;
                            }
                        }
                    }

                    if (error) {
                        log(LogLevel::WARNING,
                            "OPEN/CREATE/EXTENDED_OPEN_CREATE failed \"{:c}:\\{}\", stack_attr=0x{:04X}\n",
                            reqdrv + 'A',
                            relative_path.string(),
                            stack_attr);
                        *ax = to_little16(DOS_EXTERR_FILE_NOT_FOUND);
                    } else {
                        // success (found a file, created it or truncated it)
                        const auto handle = drive.get_handle(server_path);
                        const auto fcb_name = short_name_to_fcb(relative_path.filename().string());
                        log(LogLevel::DEBUG, "File \"{}\", handle {}\n", server_path.string(), handle);
                        log(LogLevel::DEBUG, "    FCB file name: {}\n", fcb_file_name_to_cstr(fcb_name));
                        log(LogLevel::DEBUG, "    size: {}\n", properties.size);
                        log(LogLevel::DEBUG, "    attrs: 0x{:02X}\n", properties.attrs);
                        log(LogLevel::DEBUG, "    date_time: {:04X}\n", properties.time_date);
                        if (handle == 0xFFFFU) {
                            log(LogLevel::WARNING,
                                "OPEN/CREATE/EXTENDED_OPEN_CREATE Failed to get file handle \"{:c}:\\{}\", ({})\n",
                                reqdrv + 'A',
                                relative_path.string(),
                                server_path.string());
                            return -1;
                        }
                        auto * const reply = reinterpret_cast<drive_proto_open_create_reply *>(reply_data);
                        reply->attrs = properties.attrs;
                        reply->name = fcb_name;
                        reply->date_time = to_little32(properties.time_date);
                        reply->size = to_little32(properties.size);
                        reply->start_cluster = to_little16(handle);
                        // CX result (only relevant for EXTENDED_OPEN_CREATE)
                        reply->result_code = to_little16(ext_open_create_result_code);
                        reply->mode = result_open_mode;
                        reply_packet_len = sizeof(drive_proto_open_create_reply);
                    }
                }
            } catch (const std::runtime_error & ex) {
                log(LogLevel::WARNING, "OPEN/CREATE/EXTENDED_OPEN_CREATE: {}\n", ex.what());
                *ax = to_little16(DOS_EXTERR_FILE_NOT_FOUND);
            }
        } break;

        default:  // unknown query - ignore
            return -1;
    }

    return reply_packet_len + sizeof(struct drive_proto_hdr);
}


// used for debug output of frames on screen
void dump_packet(const unsigned char * frame, int len) {
    constexpr int LINEWIDTH = 16;

    // display line by line
    const int lines = (len + LINEWIDTH - 1) / LINEWIDTH;
    for (int i = 0; i < lines; i++) {
        const int line_offset = i * LINEWIDTH;

        // output hex data
        for (int b = 0; b < LINEWIDTH; ++b) {
            const int offset = line_offset + b;
            if (b == LINEWIDTH / 2)
                print(stdout, " ");
            if (offset < len) {
                print(stdout, " {:02X}", frame[offset]);
            } else {
                print(stdout, "   ");
            }
        }

        print(stdout, " | ");  // delimiter between hex and ascii

        // output ascii data
        for (int b = 0; b < LINEWIDTH; ++b) {
            const int offset = line_offset + b;
            if (b == LINEWIDTH / 2)
                print(stdout, " ");
            if (offset >= len) {
                print(stdout, " ");
                continue;
            }
            if ((frame[offset] >= ' ') && (frame[offset] <= '~')) {
                print(stdout, "{:c}", frame[offset]);
            } else {
                print(stdout, ".");
            }
        }

        print(stdout, "\n");
    }
}


// Compute BSD Checksum for "len" bytes beginning at location "addr".
uint16_t bsd_checksum(const void * addr, uint16_t len) {
    uint16_t res;
    auto * ptr = static_cast<const uint8_t *>(addr);
    for (res = 0; len > 0; --len) {
        res = (res << 15) | (res >> 1);
        res += *ptr;
        ++ptr;
    }
    return res;
}


void print_help(const char * program_name) {
    print(
        stdout,
        "NetMount server {} , Copyright 2025 Jaroslav Rohel <jaroslav.rohel@gmail.com>\n"
        "NetMount server comes with ABSOLUTELY NO WARRANTY. This is free software\n"
        "and you are welcome to redistribute it under the terms of the GNU GPL v2.\n\n",
        PROGRAM_VERSION);

    print(stdout, "Usage:\n");
    print(
        stdout,
        "{} [--help] [--bind-addr=<IP_ADDR>] [--bind-port=<UDP_PORT] "
        "[--slip-dev=<SERIAL_DEVICE> --slip-speed=<BAUD_RATE>] [--slip-rts-cts=<ENABLED>] "
        "[--log-level=<LEVEL>] "
        "<drive>=<root_path>[,name_conversion=<method>] [... <drive>=<root_path>[,name_conversion=<method>]]\n\n",
        program_name);

    print(
        stdout,
        "Options:\n"
        "  --help                      Display this help\n"
        "  --bind-addr=<IP_ADDR>       IP address to bind to (default: \"0.0.0.0\" - all addresses). "
        "Not supported in SLIP mode\n"
        "  --bind-port=<UDP_PORT>      UDP port to listen on (default: {})\n"
        "  --slip-dev=<SERIAL_DEVICE>  Serial device used for SLIP (host network is used by default)\n"
        "  --slip-speed=<BAUD_RATE>    Baud rate of the SLIP serial device\n"
        "  --slip-rts-cts=<ENABLED>    Enable hardware flow control: 0 = OFF, 1 = ON (default: OFF)\n"
        "  --log-level=<LEVEL>         Logging verbosity level: 0 = OFF, 7 = TRACE (default: 3)\n"
        "  <drive>=<root_path>         drive - DOS drive C-Z, root_path - path to serve\n"
        "  <name_conversion>=<method>  file name conversion method: OFF, RAM (default: RAM)\n",
        DRIVE_PROTO_UDP_PORT);
}


std::string get_token(std::string_view input, char delimiter, std::size_t & offset) {
    std::string ret;

    const auto len = input.length();
    bool escape = false;
    for (; offset < len; ++offset) {
        const char ch = input[offset];
        if (escape) {
            ret += ch;
            escape = false;
        } else if (ch == '\\') {
            escape = true;
        } else if (ch == delimiter) {
            break;
        } else {
            ret += ch;
        }
    }

    return ret;
}


std::string string_ascii_to_upper(std::string input) {
    for (char & ch : input) {
        ch = ascii_to_upper(ch);
    }
    return input;
}


int parse_share_definition(std::string_view share) {
    auto drive_char = ascii_to_upper(share[0]);
    if (drive_char < 'C' || drive_char > 'Z') {
        print(stdout, "Invalid DOS drive \"{:c}\". Valid drives are in the C - Z range.\n", share[0]);
        return -1;
    }
    auto & drive = drives.at(drive_char - 'A');
    if (drive.is_shared()) {
        print(stdout, "Drive \"{:c}\" already in use.\n", drive_char);
        return -1;
    }

    std::size_t offset = 2;
    auto root_path = get_token(share, ',', offset);
    std::filesystem::path rpath;
    try {
        rpath = std::filesystem::canonical(root_path);
        if (!std::filesystem::is_directory(rpath)) {
            log(LogLevel::CRITICAL, "Path \"{}\" is not a directory\n", root_path);
            return 1;
        }
        drive.set_root(rpath);
    } catch (const std::exception & ex) {
        log(LogLevel::CRITICAL, "Failed to resolve path \"{}\": {}\n", root_path, ex.what());
        return 1;
    }

    while (++offset < share.length()) {
        const auto option = get_token(share, '=', offset);
        if (option == "name_conversion") {
            const auto value = get_token(share, ',', ++offset);
            auto upper_value = string_ascii_to_upper(value);
            log(LogLevel::INFO,
                "Set filename conversion method for drive \"{:c}\" path \"{}\" to \"{}\"\n",
                drive_char,
                drive.get_root().string(),
                upper_value);
            if (upper_value == "OFF") {
                drive.set_file_name_conversion(Drive::FileNameConversion::OFF);
                continue;
            }
            if (upper_value == "RAM") {
                drive.set_file_name_conversion(Drive::FileNameConversion::RAM);
                continue;
            }
            print(stdout, "Unknown file name conversion method \"{}\"\n", value);
            return -1;
        }
        print(stdout, "Unknown argument \"{}\"\n", option);
        return -1;
    }

    return 0;
}


}  // namespace

}  // namespace netmount_srv


using namespace netmount_srv;
int main(int argc, char ** argv) {
    std::string bind_addr;
    uint16_t bind_port = DRIVE_PROTO_UDP_PORT;
    std::string slip_dev;
    uint32_t slip_speed{0};
    bool slip_hw_flow_control{false};
    unsigned char cksumflag;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg.size() < 3) {
            print(stdout, "Invalid argument \"{}\"\n", arg);
            return -1;
        }
        if (arg == "--help") {
            print_help(argv[0]);
            return 0;
        }
        if (arg.starts_with("--bind-addr=")) {
            bind_addr = arg.substr(12);
            continue;
        }
        if (arg.starts_with("--bind-port=")) {
            char * end = nullptr;
            auto port = std::strtol(argv[i] + 12, &end, 10);
            if (port <= 0 || port > 0xFFFF || *end != '\0') {
                print(stdout, "Invalid bind port \"{}\". Valid values are in the 1-{} range.\n", argv[i] + 12, 0xFFFF);
                return -1;
            }
            bind_port = port;
            continue;
        }
        if (arg.starts_with("--slip-dev=")) {
            slip_dev = arg.substr(11);
            continue;
        }
        if (arg.starts_with("--slip-speed=")) {
            char * end = nullptr;
            auto speed = std::strtol(argv[i] + 13, &end, 10);
            if (speed < 1200 || speed > 230400 || *end != '\0') {
                print(
                    stdout,
                    "Invalid slip port speed \"{}\". Valid values are in the 1200 - 230400 range.\n",
                    argv[i] + 13);
                return -1;
            }
            slip_speed = speed;
            continue;
        }
        if (arg.starts_with("--slip-rts-cts=")) {
            slip_hw_flow_control = argv[i][15] == '1';
            if (!slip_hw_flow_control && argv[i][15] != '0') {
                print(stdout, "Invalid slip rts/cts flow control \"{}\". Valid values are 1 and 0.\n", argv[i] + 15);
                return -1;
            }
            continue;
        }
        if (arg.starts_with("--log-level=")) {
            constexpr auto MAX_LOG_LEVEL = static_cast<long>(LogLevel::TRACE);
            char * end = nullptr;
            auto log_level = std::strtol(argv[i] + 12, &end, 10) - 1;
            if (log_level < -1 || log_level > MAX_LOG_LEVEL || *end != '\0') {
                print(
                    stdout,
                    "Invalid log level \"{}\". Valid values are in the 0 - {} range.\n",
                    argv[i] + 12,
                    MAX_LOG_LEVEL + 1);
                return -1;
            }
            global_log_level = static_cast<LogLevel>(log_level);
            continue;
        }
        if (arg[1] == '=') {
            auto ret = parse_share_definition(arg);
            if (ret != 0) {
                return ret;
            }
            continue;
        }
        print(stdout, "Unknown argument \"{}\"\n", arg);
        return -1;
    }

    if (!slip_dev.empty()) {
        if (slip_speed == 0) {
            print(
                stdout,
                "Slip mode active (\"--slip-dev\" set) but \"--slip-speed\" missing. Use \"--help\" to display "
                "help.\n");
            return -1;
        }
        if (!bind_addr.empty()) {
            print(
                stdout,
                "\"--bind-addr\" is not supported in slip mode (\"--slip-dev\" is set). Use \"--help\" to display "
                "help.\n");
            return -1;
        }
    }

    bool drives_defined = false;
    for (auto & drive : drives) {
        if (drive.is_shared()) {
            drives_defined = true;
            break;
        }
    }
    if (!drives_defined) {
        print(stdout, "None shared drive defined. Use \"--help\" to display help.\n");
        return -1;
    }

    // Prepare UDP socket
    std::unique_ptr<UdpSocket> sock;
    std::unique_ptr<SlipUdpSerial> slip;
    if (slip_dev.empty()) {
        try {
            sock.reset(new UdpSocket);
            sock->bind(bind_addr.c_str(), bind_port);

            udp_socket_ptr = sock.get();
        } catch (const std::runtime_error & ex) {
            log(LogLevel::CRITICAL, "UdpSocket initialization failed: {}\n", ex.what());
            return -1;
        }
    } else {
        try {
            slip.reset(new SlipUdpSerial(slip_dev));
            slip->setup(slip_speed, slip_hw_flow_control);
        } catch (const std::runtime_error & ex) {
            log(LogLevel::CRITICAL, "SlipUdpSerial initialization failed: {}\n", ex.what());
            return -1;
        }
    }

    // setup signals handler
    signal(SIGTERM, signal_handler);
#ifdef SIGQUIT
    signal(SIGQUIT, signal_handler);
#endif
    signal(SIGINT, signal_handler);

    // Print table with shared drives
#ifdef __linux__
    bool some_drive_not_fat = false;
#endif
    for (std::size_t i = 0; i < drives.size(); ++i) {
        const auto & drive = drives[i];
        if (!drive.is_shared()) {
            continue;
        }
#ifdef __linux__
        if (!drive.is_on_fat()) {
            some_drive_not_fat = true;
        }
#endif
        print(stdout, "{:c} {:c}: => {}\n", drive.is_on_fat() ? ' ' : '*', 'A' + i, drive.get_root().string());
    }
#ifdef __linux__
    if (some_drive_not_fat) {
        print(
            stdout,
            "WARNING: It looks like drives marked with '*' are not stored on a FAT file system. "
            "DOS attributes will not be supported on these drives.\n\n");
    }
#endif

    // main loop
    try {
        uint8_t request_packet[2048];
        while (exit_flag == 0) {
            std::uint16_t request_packet_len;
            std::uint32_t last_remote_ip;
            std::uint16_t last_remote_port;
            std::string last_remote_ip_str;

            if (sock) {
                const auto wait_result = sock->wait_for_data(10000);
                switch (wait_result) {
                    case UdpSocket::WaitResult::TIMEOUT:
                        log(LogLevel::DEBUG, "sock->wait_for_data(): Timeout\n");
                        continue;
                    case UdpSocket::WaitResult::SIGNAL:
                        log(LogLevel::DEBUG, "sock->wait_for_data(): A signal was caught\n");
                        continue;
                    case UdpSocket::WaitResult::READY:
                        break;
                }

                request_packet_len = sock->receive(request_packet, sizeof(request_packet));

                last_remote_ip = sock->get_last_remote_ip();
                last_remote_port = sock->get_last_remote_port();
                last_remote_ip_str = sock->get_last_remote_ip_str();
            } else {
                request_packet_len = slip->receive();
                if (request_packet_len == 0) {
                    log(LogLevel::DEBUG, "slip->receive(): Timeout\n");
                    continue;
                }
                if (slip->get_last_dst_port() != bind_port) {
                    // Not our UDP port. Ignore packet and continue.
                    log(LogLevel::NOTICE,
                        "slip->receive(): Ignoring received UDP packet on port {}, listening on {}\n",
                        slip->get_last_dst_port(),
                        bind_port);
                    continue;
                }
                memcpy(request_packet, slip->get_last_rx_data(), request_packet_len);

                last_remote_ip = slip->get_last_remote_ip();
                last_remote_port = slip->get_last_remote_port();
                last_remote_ip_str = slip->get_last_remote_ip_str();
            }

            {
                log(LogLevel::DEBUG,
                    "Received packet, {} bytes from {}:{}\n",
                    request_packet_len,
                    last_remote_ip_str,
                    last_remote_port);

                if (request_packet_len < static_cast<int>(sizeof(struct drive_proto_hdr))) {
                    log(LogLevel::ERROR,
                        "received a truncated/malformed packet from {}:{}\n",
                        last_remote_ip_str,
                        last_remote_port);
                    continue;
                }
            }

            // check the protocol version
            auto * const header = reinterpret_cast<const drive_proto_hdr *>(request_packet);
            if (header->version != DRIVE_PROTO_VERSION) {
                log(LogLevel::ERROR,
                    "unsupported protocol version {:d} from {}:{}\n",
                    header->version,
                    last_remote_ip_str,
                    last_remote_port);
                continue;
            }

            cksumflag = from_little16(header->length_flags) >> 15;

            const uint16_t length_from_header = from_little16(header->length_flags) & 0x7FF;
            if (length_from_header < sizeof(struct drive_proto_hdr)) {
                log(LogLevel::ERROR, "received a malformed packet from {}:{}\n", last_remote_ip_str, last_remote_port);
                continue;
            }
            if (length_from_header > request_packet_len) {
                // corupted/truncated packet
                log(LogLevel::ERROR, "received a truncated packet from {}:{}\n", last_remote_ip_str, last_remote_port);
                continue;
            } else {
                if (request_packet_len != length_from_header) {
                    log(LogLevel::DEBUG,
                        "Received UDP packet with extra data at the end from {}:{} "
                        "(length in header = {}, packet len = {})\n",
                        last_remote_ip_str,
                        last_remote_port,
                        length_from_header,
                        request_packet_len);
                }
                // length_from_header seems sane, use it instead of received lenght
                request_packet_len = length_from_header;
            }

            log(LogLevel::DEBUG,
                "Received packet of {} bytes (cksum = {})\n",
                request_packet_len,
                (cksumflag != 0) ? "ENABLED" : "DISABLED");
            if (global_log_level >= LogLevel::TRACE) {
                dump_packet(request_packet, request_packet_len);
            }

#ifdef SIMULATE_PACKET_LOSS
            // simulated random input packet LOSS
            if ((rand() & 31) == 0) {
                print(stderr, "Incoming packet lost!\n");
                continue;
            }
#endif

            // check the checksum, if any
            if (cksumflag != 0) {
                const uint16_t cksum_mine = bsd_checksum(
                    &header->checksum + 1,
                    request_packet_len - (reinterpret_cast<const uint8_t *>(&header->checksum + 1) -
                                          reinterpret_cast<const uint8_t *>(header)));
                const uint16_t cksum_remote = from_little16(header->checksum);
                if (cksum_mine != cksum_remote) {
                    print(
                        stderr, "CHECKSUM MISMATCH! Computed: 0x{:04X} Received: 0x{:04X}\n", cksum_mine, cksum_remote);
                    continue;
                }
            } else {
                const uint16_t recv_magic = from_little16(header->checksum);
                if (recv_magic != DRIVE_PROTO_MAGIC) {
                    print(stderr, "Bad MAGIC! Expected: 0x{:04X} Received: 0x{:04X}\n", DRIVE_PROTO_MAGIC, recv_magic);
                    continue;
                }
            }

            auto & reply_info = answer_cache.get_reply_info(last_remote_ip, last_remote_port);
            const int send_msg_len = process_request(reply_info, request_packet, request_packet_len);
            // update reply cache entry
            if (send_msg_len >= 0) {
                reply_info.len = send_msg_len;
                reply_info.timestamp = time(NULL);
            } else {
                reply_info.len = 0;
            }

#ifdef SIMULATE_PACKET_LOSS
            // simulated random ouput packet LOSS
            if ((rand() & 31) == 0) {
                print(stderr, "Outgoing packet lost!\n");
                continue;
            }
#endif

            if (send_msg_len > 0) {
                // fill in header
                auto * const header = reinterpret_cast<struct drive_proto_hdr *>(reply_info.packet.data());
                header->length_flags = to_little16(send_msg_len);
                if (cksumflag != 0) {
                    const uint16_t checksum = bsd_checksum(
                        &header->checksum + 1,
                        send_msg_len -
                            (reinterpret_cast<uint8_t *>(&header->checksum + 1) - reinterpret_cast<uint8_t *>(header)));
                    header->checksum = to_little16(checksum);
                    header->length_flags |= to_little16(0x8000);  // set the checksum flag
                } else {
                    header->checksum = to_little16(DRIVE_PROTO_MAGIC);
                    header->length_flags &= to_little16(0x7FFF);  // zero the checksum flag
                }

                log(LogLevel::DEBUG, "Sending back an answer of {} bytes\n", send_msg_len);
                if (global_log_level >= LogLevel::TRACE) {
                    dump_packet(reply_info.packet.data(), send_msg_len);
                }
                std::uint16_t sent_bytes;
                if (sock) {
                    sent_bytes = sock->send_reply(reply_info.packet.data(), send_msg_len);
                    if (sent_bytes != send_msg_len) {
                        log(LogLevel::ERROR, "reply: {} bytes sent but {} bytes requested\n", sent_bytes, send_msg_len);
                    }
                } else {
                    try {
                        slip->send_reply(reply_info.packet.data(), send_msg_len);
                    } catch (const std::runtime_error & ex) {
                        log(LogLevel::ERROR, "send_reply: {}\n", ex.what());
                    }
                }
            } else {
                log(LogLevel::WARNING, "Request ignored: Returned {}\n", send_msg_len);
            }
        }
    } catch (const std::runtime_error & ex) {
        log(LogLevel::CRITICAL, "Exception: {}\n", ex.what());
    }

    // setup default signal handlers
    signal(SIGTERM, SIG_DFL);
#ifdef SIGQUIT
    signal(SIGQUIT, SIG_DFL);
#endif
    signal(SIGINT, SIG_DFL);

    udp_socket_ptr = nullptr;

    return 0;
}
