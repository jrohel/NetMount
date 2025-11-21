// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#include "../shared/exitcode.h"
#include "../shared/shdata.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#define NMMANAGE_VERSION "1.0.0"


#define STRINGIFY(x) #x
#define TOSTRING(x)  STRINGIFY(x)


_Packed struct install_info {
    uint8_t installed;     // is installed
    uint8_t multiplex_id;  // my multiplex_id; if i am not installed multiplex_id free to install
};

#pragma aux get_install_info parm[] modify[ax bx cx dx si di es] value struct[bx]
static struct install_info __declspec(naked) get_install_info(void) {
    // clang-format off
    __asm {
        mov ax, 0xC000  // free_id(AL) = 0, start scanning at 0xC0(AH); 0x00 - 0xBF are reserved by Microsoft
        push ax

    check_id:
        xor al, al  // subfunction 0x00 - 'installation check'
        int 0x2F

         // is it free?
        test al, al
        jnz not_free

        // it's free - remember it
        pop ax
        mov al, ah
        push ax
        jmp check_next_id

    not_free:
        // is it NetMount client?
        cmp al, 0xFF
        jne check_next_id
        cmp bx, 'JA'
        jne check_next_id
        cmp cx, 'RO'
        jne check_next_id
        cmp dx, 'NM'
        jne check_next_id

        // NetMount client found
        pop ax
        mov al, 1  // installed AL = 1
        ret

    check_next_id:
        // not me, check next id
        pop ax
        inc ah
        push ax
        jnz check_id  // if cur_id is zero then the entire range (C0..FF) has been checked

        pop ax
        mov ah, al
        mov al, 0  // not installed AL = 0
        ret
    }
    // clang-format on
}


static struct shared_data __far * get_installed_shared_data_ptr(uint8_t multiplex_id);
#pragma aux get_installed_shared_data_ptr = \
    "mov al, 1"                             \
    "mov bx, 1"                             \
    "int 0x2F" parm[ah] modify exact[ax bx cx] value[cx bx]


static void nmmanage_info(void) {
    printf(
        "NMManage\n"
        "Version: %s\n"
        "ABI version: %d\n",
        NMMANAGE_VERSION,
        ABI_VERSION);
}


static void netmount_info(const struct shared_data __far * data) {
    printf(
        "Detected installed NetMount client\n"
        "Version: %Fs\n"
        "ABI version: %d\n"
        "Minimum compatible ABI version: %d\n",
        data->netmount_version,
        data->abi_version,
        data->min_compatible_abi_version);
}


static void net_info(const struct shared_data __far * data) {
    printf(
        "IP: %d.%d.%d.%d\n",
        data->local_ipv4.bytes[0],
        data->local_ipv4.bytes[1],
        data->local_ipv4.bytes[2],
        data->local_ipv4.bytes[3]);

    printf(
        "MASK: %d.%d.%d.%d\n",
        data->net_mask.bytes[0],
        data->net_mask.bytes[1],
        data->net_mask.bytes[2],
        data->net_mask.bytes[3]);

    const uint8_t gw_ip_slot = data->gateway_ip_slot;
    if (gw_ip_slot != 0xFF) {
        const union ipv4_addr gw = data->ip_mac_map[gw_ip_slot].ip;
        printf("GW: %d.%d.%d.%d\n", gw.bytes[0], gw.bytes[1], gw.bytes[2], gw.bytes[3]);
    } else {
        printf("GW:\n");
    }

    printf("Local udp PORT: %d\n", data->local_port);

    printf("Interface MTU: %d\n", data->interface_mtu);

    printf("Send ARP_REQUESTS: %s\n", data->disable_sending_arp_request ? "DISABLED" : "ENABLED");

    printf("PKT_INT: 0x%02X\n", data->used_pktdrv_int);

    printf(
        "MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
        data->local_mac_addr.bytes[0],
        data->local_mac_addr.bytes[1],
        data->local_mac_addr.bytes[2],
        data->local_mac_addr.bytes[3],
        data->local_mac_addr.bytes[4],
        data->local_mac_addr.bytes[5]);
}


static int net_get(const struct shared_data __far * restrict data, const char * restrict option) {
    if (strcasecmp(option, "IP") == 0) {
        printf(
            "%d.%d.%d.%d\n",
            data->local_ipv4.bytes[0],
            data->local_ipv4.bytes[1],
            data->local_ipv4.bytes[2],
            data->local_ipv4.bytes[3]);
    } else if (strcasecmp(option, "MASK") == 0) {
        printf(
            "%d.%d.%d.%d\n",
            data->net_mask.bytes[0],
            data->net_mask.bytes[1],
            data->net_mask.bytes[2],
            data->net_mask.bytes[3]);
    } else if (strcasecmp(option, "GW") == 0) {
        const uint8_t gw_ip_slot = data->gateway_ip_slot;
        if (gw_ip_slot != 0xFF) {
            const union ipv4_addr gw = data->ip_mac_map[gw_ip_slot].ip;
            printf("%d.%d.%d.%d\n", gw.bytes[0], gw.bytes[1], gw.bytes[2], gw.bytes[3]);
        } else {
            printf("\n");
        }
    } else if (strcasecmp(option, "PORT") == 0) {
        printf("%d\n", data->local_port);
    } else if (strcasecmp(option, "MTU") == 0) {
        printf("%d\n", data->interface_mtu);
    } else if (strcasecmp(option, "ARP_REQUESTS") == 0) {
        printf("%s\n", data->disable_sending_arp_request ? "DISABLED" : "ENABLED");
    } else if (strcasecmp(option, "PKT_INT") == 0) {
        printf("0x%02X\n", data->used_pktdrv_int);
    } else if (strcasecmp(option, "MAC") == 0) {
        printf(
            "%02X:%02X:%02X:%02X:%02X:%02X\n",
            data->local_mac_addr.bytes[0],
            data->local_mac_addr.bytes[1],
            data->local_mac_addr.bytes[2],
            data->local_mac_addr.bytes[3],
            data->local_mac_addr.bytes[4],
            data->local_mac_addr.bytes[5]);
    } else {
        printf("Error: Unknown option: %s\n", option);
        return EXIT_BAD_ARG;
    }

    return EXIT_OK;
}


static int net_set(
    struct shared_data __far * restrict data, const char * restrict option, const char * restrict value) {
    if (strcasecmp(option, "PORT") == 0) {
        char * endptr;
        uint16_t port = strtoul(value, &endptr, 0);
        if (endptr == value || *endptr != '\0') {
            printf("Error: The value is not a valid number: %s\n", value);
            return EXIT_BAD_ARG;
        }
        if (port == 0) {
            printf("Error: Local UDP port must be in range 1-65535\n");
            return EXIT_BAD_ARG;
        }
        data->local_port = port;
    } else if (strcasecmp(option, "MTU") == 0) {
        char * endptr;
        uint16_t mtu = strtoul(value, &endptr, 0);
        if (endptr == value || *endptr != '\0') {
            printf("Error: The value is not a valid number: %s\n", value);
            return EXIT_BAD_ARG;
        }
        if (mtu > MAX_MTU || mtu < MIN_MTU) {
            printf("Error: Interface MTU must be in the range " TOSTRING(MIN_MTU) "-" TOSTRING(MAX_MTU) "\n");
            return EXIT_BAD_ARG;
        }
        data->interface_mtu = mtu;
    } else if (strcasecmp(option, "ARP_REQUESTS") == 0) {
        if (strcasecmp(value, "ENABLED") == 0) {
            data->disable_sending_arp_request = 0;
        } else if (strcasecmp(value, "DISABLED") == 0) {
            data->disable_sending_arp_request = 1;
        } else {
            printf("Error: Only ENABLED and DISABLED are valid values: %s\n", value);
            return EXIT_BAD_ARG;
        }
    } else if (
        strcasecmp(option, "IP") == 0 || strcasecmp(option, "MASK") == 0 || strcasecmp(option, "GW") == 0 ||
        strcasecmp(option, "PKT_INT") == 0 || strcasecmp(option, "MAC") == 0) {
        printf("Error: Option is read-only and cannot be modified: %s\n", option);
        return EXIT_BAD_ARG;
    } else {
        printf("Error: Unknown option: %s\n", option);
        return EXIT_BAD_ARG;
    }

    return EXIT_OK;
}


static void drive_list(const struct shared_data __far * data) {
    for (int drive_no = 2; drive_no < sizeof(data->drives) / sizeof(data->drives[0]); ++drive_no) {
        if (data->drive_map[drive_no] != 0xFF) {
            const struct drive_info __far * drive = data->drives + drive_no;
            const union ipv4_addr ip = data->ip_mac_map[drive->remote_ip_idx].ip;
            printf(
                "%c -> %d.%d.%d.%d:%d/%c\n",
                drive_no + 'A',
                ip.bytes[0],
                ip.bytes[1],
                ip.bytes[2],
                ip.bytes[3],
                drive->remote_port,
                data->drive_map[drive_no] + 'A');
        }
    }
}


static uint8_t compute_rcv_tmo(uint8_t rcv_tmo_18_2_ticks_shr_2) {
    uint16_t min_dsec = (((uint32_t)rcv_tmo_18_2_ticks_shr_2 << 2) * 100) / 182;
    uint8_t min_sec = min_dsec / 10;
    min_dsec -= min_sec * 10;
    if (min_dsec >= 5) {
        ++min_sec;
    }
    return min_sec;
}


static int drive_info(const struct shared_data __far * data, uint8_t drive_no) {
    if (data->drive_map[drive_no] == 0xFF) {
        printf("Drive %c is not mounted by NetMount\n", drive_no + 'A');
        return EXIT_DRIVE_NOT_MOUNTED;
    }

    const struct drive_info __far * drive = data->drives + drive_no;
    const union ipv4_addr ip = data->ip_mac_map[drive->remote_ip_idx].ip;
    printf("Local drive: %c\n", drive_no + 'A');
    printf("Server IP: %d.%d.%d.%d\n", ip.bytes[0], ip.bytes[1], ip.bytes[2], ip.bytes[3]);
    printf("Server udp PORT: %d\n", drive->remote_port);
    printf("Server DRIVE: %c\n", data->drive_map[drive_no] + 'A');
    printf("Minimum length of data block read from the server MIN_READ_LEN [bytes]: %d\n", drive->min_server_read_len);
    printf(
        "Minimum response timenout MIN_RCV_TMO [seconds]: %d\n", compute_rcv_tmo(drive->min_rcv_tmo_18_2_ticks_shr_2));
    printf(
        "Maximum response timenout MAX_RCV_TMO [seconds]: %d\n", compute_rcv_tmo(drive->max_rcv_tmo_18_2_ticks_shr_2));
    printf("Maximum number of request retries MAX_RETRIES: %d\n", drive->max_request_retries);
    printf(
        "Netmount protocol checksum CHECKSUM_NETMOUNT: %s\n",
        drive->enabled_checksums & CHECKSUM_NETMOUNT_PROTO ? "ENABLED" : "DISABLED");
    printf(
        "IP header checksum CHECKSUM_IP_HEADER: %s\n",
        drive->enabled_checksums & CHECKSUM_IP_HEADER ? "ENABLED" : "Send only (ignore received)");

    return EXIT_OK;
}


static int drive_get(const struct shared_data __far * restrict data, uint8_t drive_no, const char * restrict option) {
    if (data->drive_map[drive_no] == 0xFF) {
        printf("Drive %c is not mounted by NetMount\n", drive_no + 'A');
        return EXIT_DRIVE_NOT_MOUNTED;
    }

    const struct drive_info __far * drive = data->drives + drive_no;
    if (strcasecmp(option, "IP") == 0) {
        const union ipv4_addr ip = data->ip_mac_map[drive->remote_ip_idx].ip;
        printf("%d.%d.%d.%d\n", ip.bytes[0], ip.bytes[1], ip.bytes[2], ip.bytes[3]);
    } else if (strcasecmp(option, "PORT") == 0) {
        printf("%d\n", drive->remote_port);
    } else if (strcasecmp(option, "DRIVE") == 0) {
        printf("%c\n", data->drive_map[drive_no] + 'A');
    } else if (strcasecmp(option, "MIN_READ_LEN") == 0) {
        printf("%d\n", drive->min_server_read_len);
    } else if (strcasecmp(option, "MIN_RCV_TMO") == 0) {
        printf("%d\n", compute_rcv_tmo(drive->min_rcv_tmo_18_2_ticks_shr_2));
    } else if (strcasecmp(option, "MAX_RCV_TMO") == 0) {
        printf("%d\n", compute_rcv_tmo(drive->max_rcv_tmo_18_2_ticks_shr_2));
    } else if (strcasecmp(option, "MAX_RETRIES") == 0) {
        printf("%d\n", drive->max_request_retries);
    } else if (strcasecmp(option, "CHECKSUM_NETMOUNT") == 0) {
        printf("%s\n", drive->enabled_checksums & CHECKSUM_NETMOUNT_PROTO ? "ENABLED" : "DISABLED");
    } else if (strcasecmp(option, "CHECKSUM_IP_HEADER") == 0) {
        printf("%s\n", drive->enabled_checksums & CHECKSUM_IP_HEADER ? "ENABLED" : "SEND_ONLY");
    } else {
        printf("Error: Unknown option: %s\n", option);
        return EXIT_BAD_ARG;
    }

    return EXIT_OK;
}


static int drive_set(
    struct shared_data __far * restrict data,
    uint8_t drive_no,
    const char * restrict option,
    const char * restrict value) {
    if (data->drive_map[drive_no] == 0xFF) {
        printf("Drive %c is not mounted by NetMount\n", drive_no + 'A');
        return EXIT_DRIVE_NOT_MOUNTED;
    }

    struct drive_info __far * drive = data->drives + drive_no;
    if (strcasecmp(option, "MIN_READ_LEN") == 0) {
        // There is a risk that if a user reads fewer bytes from a file than the original minimum data read length
        // before changing it (thus filling the read cache), then increases the minimum data read length and,
        // within 5 seconds (the cache is automatically invalidated after 5 seconds), requests fewer bytes of data
        // than the new minimum data read length, part of which should lie within the read cache, they may read a few
        // bytes of invalid data.
        char * endptr;
        uint16_t min_server_read_len = strtoul(value, &endptr, 0);
        if (endptr == value || *endptr != '\0') {
            printf("Error: The value is not a valid number: %s\n", value);
            return EXIT_BAD_ARG;
        }
        // The value must be a power of two and no greater than FILE_BUFFER_SIZE.
        if (min_server_read_len > MAX_MIN_READ_LEN || (min_server_read_len & (min_server_read_len - 1)) != 0) {
            printf(
                "Error: Minimum read length must be power of 2 in range " TOSTRING(MIN_MIN_READ_LEN) "-" TOSTRING(
                    MAX_MIN_READ_LEN) "\n");
            return EXIT_BAD_ARG;
        }
        drive->min_server_read_len = min_server_read_len;
    } else if (strcasecmp(option, "MIN_RCV_TMO") == 0) {
        char * endptr;
        uint16_t min_rcv_tmo_sec = strtoul(value, &endptr, 0);
        if (endptr == value || *endptr != '\0') {
            printf("Error: The value is not a valid number: %s\n", value);
            return EXIT_BAD_ARG;
        }
        if (min_rcv_tmo_sec < MIN_MIN_RCV_TMO_SEC || min_rcv_tmo_sec > MAX_MIN_RCV_TMO_SEC) {
            printf(
                "Error: Minimum response timeout must be in range " TOSTRING(MIN_MIN_RCV_TMO_SEC) "-" TOSTRING(
                    MAX_MIN_RCV_TMO_SEC) "\n");
            return EXIT_BAD_ARG;
        }
        drive->min_rcv_tmo_18_2_ticks_shr_2 = ((min_rcv_tmo_sec * 182) / 10) >> 2;
    } else if (strcasecmp(option, "MAX_RCV_TMO") == 0) {
        char * endptr;
        uint16_t max_rcv_tmo_sec = strtoul(value, &endptr, 0);
        if (endptr == value || *endptr != '\0') {
            printf("Error: The value is not a valid number: %s\n", value);
            return EXIT_BAD_ARG;
        }
        if (max_rcv_tmo_sec < MIN_MAX_RCV_TMO_SEC || max_rcv_tmo_sec > MAX_MAX_RCV_TMO_SEC) {
            printf(
                "Error: Maximum response timeout must be in range " TOSTRING(MIN_MAX_RCV_TMO_SEC) "-" TOSTRING(
                    MAX_MAX_RCV_TMO_SEC) "\n");
            return EXIT_BAD_ARG;
        }
        drive->max_rcv_tmo_18_2_ticks_shr_2 = ((max_rcv_tmo_sec * 182) / 10) >> 2;
    } else if (strcasecmp(option, "MAX_RETRIES") == 0) {
        char * endptr;
        int16_t max_request_retries = strtoul(value, &endptr, 0);
        if (endptr == value || *endptr != '\0') {
            printf("Error: The value is not a valid number: %s\n", value);
            return EXIT_BAD_ARG;
        }
        if (max_request_retries > MAX_MAX_RETRIES || max_request_retries < MIN_MAX_RETRIES) {
            printf(
                "Error: Maximum request retries must be in range " TOSTRING(MIN_MAX_RETRIES) "-" TOSTRING(
                    MAX_MAX_RETRIES) "\n");
            return EXIT_BAD_ARG;
        }
        drive->max_request_retries = max_request_retries;
    } else if (strcasecmp(option, "CHECKSUM_NETMOUNT") == 0) {
        if (strcasecmp(value, "ENABLED") == 0) {
            drive->enabled_checksums |= CHECKSUM_NETMOUNT_PROTO;
        } else if (strcasecmp(value, "DISABLED") == 0) {
            drive->enabled_checksums &= ~CHECKSUM_NETMOUNT_PROTO;
        } else {
            printf("Error: Only ENABLED and DISABLED are valid values: %s\n", value);
            return EXIT_BAD_ARG;
        }
    } else if (strcasecmp(option, "CHECKSUM_IP_HEADER") == 0) {
        if (strcasecmp(value, "ENABLED") == 0) {
            drive->enabled_checksums |= CHECKSUM_IP_HEADER;
        } else if (strcasecmp(value, "SEND_ONLY") == 0) {
            drive->enabled_checksums &= ~CHECKSUM_IP_HEADER;
        } else {
            printf("Error: Only ENABLED and SEND_ONLY are valid values: %s\n", value);
            return EXIT_BAD_ARG;
        }
    } else if (strcasecmp(option, "IP") == 0 || strcasecmp(option, "PORT") == 0 || strcasecmp(option, "DRIVE") == 0) {
        printf("Error: Option is read-only and cannot be modified: %s\n", option);
        return EXIT_BAD_ARG;
    } else {
        printf("Error: Unknown option: %s\n", option);
        return EXIT_BAD_ARG;
    }

    return EXIT_OK;
}


static void mac_list(const struct shared_data __far * data) {
    for (int mac_idx = 0; mac_idx < sizeof(data->ip_mac_map) / sizeof(data->ip_mac_map[0]); ++mac_idx) {
        const union ipv4_addr ip = data->ip_mac_map[mac_idx].ip;
        if (ip.value != 0xFFFFFFFFUL) {
            // Table slot is in use, print line
            const struct mac_addr mac = data->ip_mac_map[mac_idx].mac_addr;
            printf(
                "%03d.%03d.%03d.%03d   %02X:%02X:%02X:%02X:%02X:%02X\n",
                ip.bytes[0],
                ip.bytes[1],
                ip.bytes[2],
                ip.bytes[3],
                mac.bytes[0],
                mac.bytes[1],
                mac.bytes[2],
                mac.bytes[3],
                mac.bytes[4],
                mac.bytes[5]);
        }
    }
}


static union ipv4_addr parse_ipv4_addr(const char * restrict str, int * restrict error, const char ** restrict endptr) {
    union ipv4_addr addr;
    const char * const orig_str = str;
    char * local_endptr;
    int idx;
    int err = 0;
    for (idx = 0; idx < 3; ++idx) {
        unsigned long value = strtoul(str, &local_endptr, 10);
        if (value > 255 || local_endptr == str || *local_endptr != '.') {
            err = 1;
            break;
        }
        addr.bytes[idx] = value;
        str = local_endptr + 1;
    }
    if (!err) {
        unsigned long value = strtoul(str, &local_endptr, 10);
        if (value > 255 || local_endptr == str || *local_endptr != '\0') {
            err = 1;
        }
        addr.bytes[idx] = value;
    }
    if (err) {
        printf("Error: The value is not a valid IPv4 address: %s\n", orig_str);
    }
    if (error) {
        *error = err;
    }
    if (endptr) {
        *endptr = local_endptr;
    }
    return addr;
}


static struct mac_addr parse_mac_addr(const char * restrict str, int * restrict error, const char ** restrict endptr) {
    struct mac_addr addr;
    const char * const orig_str = str;
    char * local_endptr;
    int idx;
    int err = 0;
    for (idx = 0; idx < 5; ++idx) {
        unsigned long value = strtoul(str, &local_endptr, 16);
        if (value > 255 || local_endptr == str || *local_endptr != ':') {
            err = 1;
            break;
        }
        addr.bytes[idx] = value;
        str = local_endptr + 1;
    }
    if (!err) {
        unsigned long value = strtoul(str, &local_endptr, 16);
        if (value > 255 || local_endptr == str || *local_endptr != '\0') {
            err = 1;
        }
        addr.bytes[idx] = value;
    }
    if (err) {
        printf("Error: The value is not a valid MAC address: %s\n", orig_str);
    }
    if (error) {
        *error = err;
    }
    if (endptr) {
        *endptr = local_endptr;
    }
    return addr;
}


static int mac_get(const struct shared_data __far * restrict data, const char * restrict ipv4_addr_str) {
    int err;
    const union ipv4_addr ip = parse_ipv4_addr(ipv4_addr_str, &err, NULL);
    if (err) {
        return EXIT_BAD_ARG;
    }

    if (ip.value == 0xFFFFFFFFUL) {
        printf("Error: The specified IP address is a broadcast address: %s\n", ipv4_addr_str);
        return EXIT_UNKNOWN_ARG;
    }

    for (int mac_idx = 0; mac_idx < sizeof(data->ip_mac_map) / sizeof(data->ip_mac_map[0]); ++mac_idx) {
        if (data->ip_mac_map[mac_idx].ip.value == ip.value) {
            // IP found
            const struct mac_addr mac = data->ip_mac_map[mac_idx].mac_addr;
            printf(
                "%02X:%02X:%02X:%02X:%02X:%02X\n",
                mac.bytes[0],
                mac.bytes[1],
                mac.bytes[2],
                mac.bytes[3],
                mac.bytes[4],
                mac.bytes[5]);
            return EXIT_OK;
        }
    }

    printf("Error: IP address not in NetMount IP-to-MAC table: %s\n", ipv4_addr_str);
    return EXIT_UNKNOWN_ARG;
}


static int mac_set(
    struct shared_data __far * restrict data, const char * restrict ipv4_addr_str, const char * restrict mac_addr_str) {
    int err;
    const union ipv4_addr ip = parse_ipv4_addr(ipv4_addr_str, &err, NULL);
    if (err) {
        return EXIT_BAD_ARG;
    }

    if (ip.value == 0xFFFFFFFFUL) {
        printf("Error: The specified IP address is a broadcast address: %s\n", ipv4_addr_str);
        return EXIT_UNKNOWN_ARG;
    }

    const struct mac_addr mac = parse_mac_addr(mac_addr_str, &err, NULL);
    if (err) {
        return EXIT_BAD_ARG;
    }

    for (int mac_idx = 0; mac_idx < sizeof(data->ip_mac_map) / sizeof(data->ip_mac_map[0]); ++mac_idx) {
        if (data->ip_mac_map[mac_idx].ip.value == ip.value) {
            // IP found
            data->ip_mac_map[mac_idx].mac_addr = mac;
            return EXIT_OK;
        }
    }

    printf("Error: IP address not in NetMount IP-to-MAC table: %s\n", ipv4_addr_str);
    return EXIT_UNKNOWN_ARG;
}


static void print_help(void) {
    printf(
        "NMManage " NMMANAGE_VERSION
        "\n"
        "Copyright 2025 Jaroslav Rohel <jaroslav.rohel@gmail.com>\n"
        "NMManage comes with ABSOLUTELY NO WARRANTY.\n"
        "This is free software, and you are welcome to redistribute it\n"
        "under the terms of the GNU General Public License, version 2.\n"
        "\n"
        "Usage:\n"
        "NMMANAGE INFO\n"
        "NMMANAGE NET INFO\n"
        "NMMANAGE NET GET <get_net_option>\n"
        "NMMANAGE NET SET <set_net_option> <value>\n"
        "NMMANAGE DRIVES\n"
        "NMMANAGE DRIVE <local_drive_letter> INFO\n"
        "NMMANAGE DRIVE <local_drive_letter> GET <get_drive_option>\n"
        "NMMANAGE DRIVE <local_drive_letter> SET <set_drive_option> <value>\n"
        "NMMANAGE MACS\n"
        "NMMANAGE MAC GET <ipv4_addr>\n"
        "NMMANAGE MAC SET <ipv4_addr> <mac_addr>\n"
        "\n"
        "Commands:\n"
        "INFO                  Show info about NMManager and detected installed NetMount\n"
        "NET INFO              Show current NetMount network settings\n"
        "NET GET               Get value of a network option\n"
        "NET SET               Set value of a network option\n"
        "DRIVES                List all mounted network drives\n"
        "DRIVE INFO            Show details (all options) for a specific mounted drive\n"
        "DRIVE GET             Get value of a drive mount option\n"
        "DRIVE SET             Set value of a drive mount option\n"
        "MACS                  List IP-to-MAC address table entries\n"
        "MAC GET               Get MAC address for IP\n"
        "MAC SET               Set MAC address for IP\n"
        "/?                    Display this help\n"
        "\n"
        "Arguments:\n"
        "<local_drive_letter>  Specifies the mounted drive to work with (e.g. H)\n"
        "<get_net_option>      IP, MASK, GW, PORT, MTU, ARP_REQUESTS, PKT_INT, MAC\n"
        "<set_net_option>      PORT, MTU, ARP_REQUESTS\n"
        "<get_drive_option>    IP, PORT, DRIVE, MIN_READ_LEN, MIN_RCV_TMO, MAX_RCV_TMO,\n"
        "                      MAX_RETRIES, CHECKSUM_NETMOUNT, CHECKSUM_IP_HEADER\n"
        "<set_drive_option>    MIN_READ_LEN, MIN_RCV_TMO, MAX_RCV_TMO,\n"
        "                      MAX_RETRIES, CHECKSUM_NETMOUNT, CHECKSUM_IP_HEADER\n"
        "<value>               Specifies value to set\n"
        "<ipv4_addr>           Specifies the IPv4 address to work with\n"
        "<mac_addr>            Specifies the MAC address to set\n");
}


int main(int argc, char * argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '/' && argv[i][1] == '?' && argv[i][2] == '\0') {
            print_help();
            return EXIT_OK;
        }
    }

    if (argc < 2) {
        printf("Error: Missing command. Use \"/?\" to display help.\n");
        return EXIT_MISSING_CMD;
    }

    const struct install_info info = get_install_info();
    if (!info.installed) {
        printf("NetMount client is not installed\n");
        return EXIT_NOT_INSTALLED;
    }

    struct shared_data __far * const data = get_installed_shared_data_ptr(info.multiplex_id);

    if (data->abi_version < ABI_VERSION || data->min_compatible_abi_version > ABI_VERSION) {
        printf("NMManage is not compatible with installed NetMount client\n");
        nmmanage_info();
        printf("\n");
        netmount_info(data);
        return EXIT_INCOMPATIBLE_VERSION;
    }

    const char * command = argv[1];
    if (strcasecmp(command, "INFO") == 0) {
        if (argc > 2) {
            printf("Error: Too many arguments. Use \"/?\" to display help.\n");
            return EXIT_UNKNOWN_ARG;
        }
        nmmanage_info();
        printf("\n");
        netmount_info(data);
        return EXIT_OK;
    } else if (strcasecmp(command, "NET") == 0) {
        if (argc < 3) {
            printf("Error: Missing argument. Use \"/?\" to display help.\n");
            return EXIT_MISSING_ARG;
        }
        const char * arg = argv[2];
        if (strcasecmp(arg, "INFO") == 0) {
            if (argc > 3) {
                printf("Error: Too many arguments. Use \"/?\" to display help.\n");
                return EXIT_UNKNOWN_ARG;
            }
            net_info(data);
            return EXIT_OK;
        }
        if (strcasecmp(arg, "GET") == 0) {
            if (argc < 4) {
                printf("Error: Missing argument. Use \"/?\" to display help.\n");
                return EXIT_MISSING_ARG;
            }
            if (argc > 4) {
                printf("Error: Too many arguments. Use \"/?\" to display help.\n");
                return EXIT_UNKNOWN_ARG;
            }
            return net_get(data, argv[3]);
        }
        if (strcasecmp(arg, "SET") == 0) {
            if (argc < 5) {
                printf("Error: Missing argument. Use \"/?\" to display help.\n");
                return EXIT_MISSING_ARG;
            }
            if (argc > 5) {
                printf("Error: Too many arguments. Use \"/?\" to display help.\n");
                return EXIT_UNKNOWN_ARG;
            }
            return net_set(data, argv[3], argv[4]);
        }
        printf("Error: Unknown argument: %s\n", arg);
        return EXIT_UNKNOWN_ARG;
    } else if (strcasecmp(command, "DRIVES") == 0) {
        if (argc > 2) {
            printf("Error: Too many arguments. Use \"/?\" to display help.\n");
            return EXIT_UNKNOWN_ARG;
        }
        drive_list(data);
        return EXIT_OK;
    } else if (strcasecmp(command, "DRIVE") == 0) {
        if (argc < 4) {
            printf("Error: Missing argument. Use \"/?\" to display help.\n");
            return EXIT_MISSING_ARG;
        }

        const uint8_t drive_no = toupper(argv[2][0]) - 'A';
        if (drive_no >= MAX_DRIVES_COUNT) {
            printf("Error: Bad local drive letter\n");
            return EXIT_BAD_DRIVE_LETTER;
        }

        const char * const arg = argv[3];
        if (strcasecmp(arg, "INFO") == 0) {
            if (argc > 4) {
                printf("Error: Too many arguments. Use \"/?\" to display help.\n");
                return EXIT_UNKNOWN_ARG;
            }
            return drive_info(data, drive_no);
        }
        if (strcasecmp(arg, "GET") == 0) {
            if (argc < 5) {
                printf("Error: Missing argument. Use \"/?\" to display help.\n");
                return EXIT_MISSING_ARG;
            }
            if (argc > 5) {
                printf("Error: Too many arguments. Use \"/?\" to display help.\n");
                return EXIT_UNKNOWN_ARG;
            }
            return drive_get(data, drive_no, argv[4]);
        }
        if (strcasecmp(arg, "SET") == 0) {
            if (argc < 6) {
                printf("Error: Missing argument. Use \"/?\" to display help.\n");
                return EXIT_MISSING_ARG;
            }
            if (argc > 6) {
                printf("Error: Too many arguments. Use \"/?\" to display help.\n");
                return EXIT_UNKNOWN_ARG;
            }
            return drive_set(data, drive_no, argv[4], argv[5]);
        }
        printf("Error: Unknown argument: %s\n", arg);
        return EXIT_UNKNOWN_ARG;
    } else if (strcasecmp(command, "MACS") == 0) {
        if (argc > 2) {
            printf("Error: Too many arguments. Use \"/?\" to display help.\n");
            return EXIT_UNKNOWN_ARG;
        }
        mac_list(data);
        return EXIT_OK;
    } else if (strcasecmp(command, "MAC") == 0) {
        if (argc < 3) {
            printf("Error: Missing argument. Use \"/?\" to display help.\n");
            return EXIT_MISSING_ARG;
        }
        const char * arg = argv[2];
        if (strcasecmp(arg, "GET") == 0) {
            if (argc < 4) {
                printf("Error: Missing argument. Use \"/?\" to display help.\n");
                return EXIT_MISSING_ARG;
            }
            if (argc > 4) {
                printf("Error: Too many arguments. Use \"/?\" to display help.\n");
                return EXIT_UNKNOWN_ARG;
            }
            return mac_get(data, argv[3]);
        }
        if (strcasecmp(arg, "SET") == 0) {
            if (argc < 5) {
                printf("Error: Missing argument. Use \"/?\" to display help.\n");
                return EXIT_MISSING_ARG;
            }
            if (argc > 5) {
                printf("Error: Too many arguments. Use \"/?\" to display help.\n");
                return EXIT_UNKNOWN_ARG;
            }
            return mac_set(data, argv[3], argv[4]);
        }
        printf("Error: Unknown argument: %s\n", arg);
        return EXIT_UNKNOWN_ARG;
    }

    printf("Error: Unknown command: %s\n", argv[1]);
    return EXIT_UNKNOWN_CMD;
}
