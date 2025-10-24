// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#include "../include/exitcode.h"
#include "../include/shdata.h"

#include <ctype.h>
#include <stdio.h>
#include <strings.h>

#define PROGRAM_VERSION "1.0.0"

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


static void net_info(struct shared_data __far * data) {
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

    printf("UDP PORT: %d\n", data->local_port);

    printf("INTERFACE MTU: %d\n", data->interface_mtu);
}


static void drive_list(struct shared_data __far * data) {
    for (int drive_no = 2; drive_no < sizeof(data->drives) / sizeof(data->drives[0]); ++drive_no) {
        if (data->ldrv[drive_no] != 0xFF) {
            struct drive_info __far * drive = data->drives + drive_no;
            const union ipv4_addr ip = data->ip_mac_map[drive->remote_ip_idx].ip;
            printf(
                "%c -> %d.%d.%d.%d:%d/%c\n",
                drive_no + 'A',
                ip.bytes[0],
                ip.bytes[1],
                ip.bytes[2],
                ip.bytes[3],
                drive->remote_port,
                data->ldrv[drive_no] + 'A');
        }
    }
}


static int drive_info(struct shared_data __far * data, uint8_t drive_no) {
    if (data->ldrv[drive_no] == 0xFF) {
        printf("Drive %c is not mounted by NetMount\n", drive_no + 'A');
        return EXIT_DRIVE_NOT_MOUNTED;
    }
    struct drive_info __far * drive = data->drives + drive_no;
    const union ipv4_addr ip = data->ip_mac_map[drive->remote_ip_idx].ip;
    printf("Drive: %c\n", drive_no + 'A');
    printf(
        "Remote: %d.%d.%d.%d:%d/%c\n",
        ip.bytes[0],
        ip.bytes[1],
        ip.bytes[2],
        ip.bytes[3],
        drive->remote_port,
        data->ldrv[drive_no] + 'A');
    printf("Minimum length of data block read from the server [bytes]: %d\n", drive->min_server_read_len);
    uint16_t min_dsec = (((uint32_t)drive->min_rcv_tmo_18_2_ticks_shr_2 << 2) * 100) / 182;
    uint8_t min_sec = min_dsec / 10;
    min_dsec -= min_sec * 10;
    printf("Minimum response timenout [seconds]: %d.%d\n", min_sec, min_dsec);
    uint16_t max_dsec = (((uint32_t)drive->max_rcv_tmo_18_2_ticks_shr_2 << 2) * 100) / 182;
    uint8_t max_sec = max_dsec / 10;
    max_dsec -= max_sec * 10;
    printf("Maximum response timenout [seconds]: %d.%d\n", max_sec, max_dsec);
    printf("Maximum number of request retries: %d\n", drive->max_request_retries);
    printf(
        "Netmount protocol checksum: %s\n",
        drive->enabled_checksums & CHECKSUM_NETMOUNT_PROTO ? "ENABLED" : "DISABLED");
    printf(
        "IP header checksum: %s\n",
        drive->enabled_checksums & CHECKSUM_IP_HEADER ? "ENABLED" : "Send only (ignore received)");
    return EXIT_OK;
}


static void print_help(void) {
    printf(
        "NUtils " PROGRAM_VERSION
        "\n"
        "Copyright 2025 Jaroslav Rohel <jaroslav.rohel@gmail.com>\n"
        "NUtils comes with ABSOLUTELY NO WARRANTY.\n"
        "This is free software, and you are welcome to redistribute it\n"
        "under the terms of the GNU General Public License, version 2.\n"
        "\n"
        "Usage:\n"
        "NUTILS NET INFO\n"
        "NUTILS DRIVE LIST\n"
        "NUTILS DRIVE INFO <local_drive_letter>\n"
        "\n"
        "Commands:\n"
        "NET INFO                  Show current NetMount client status\n"
        "DRIVE LIST                List all mounted network drives\n"
        "DRIVE INFO                Show details for a specific mounted drive\n"
        "\n"
        "Arguments:\n"
        "<local_drive_letter>      Specifies mounted drive to work with (e.g. H)\n"
        "/?                        Display this help\n");
}


int main(int argc, char * argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '/' && argv[i][1] == '?' && argv[i][2] == '\0') {
            print_help();
            return EXIT_OK;
        }
    }

    if (argc < 2) {
        printf("Missing command. Use \"/?\" to display help.\n");
        return EXIT_MISSING_CMD;
    }

    const struct install_info info = get_install_info();
    if (!info.installed) {
        printf("NetMount client is not installed\n");
        return EXIT_NOT_INSTALLED;
    }

    struct shared_data __far * data = get_installed_shared_data_ptr(info.multiplex_id);

    const char * command = argv[1];
    if (strcasecmp(command, "NET") == 0) {
        if (argc < 3) {
            printf("Error: Missing argument. Use \"/?\" to display help.\n");
            return EXIT_MISSING_ARG;
        }
        const char * arg = argv[2];
        if (strcasecmp(arg, "INFO") == 0) {
            net_info(data);
            return EXIT_OK;
        }
        printf("Error: Unknown argument: %s\n", arg);
        return EXIT_UNKNOWN_ARG;
    } else if (strcasecmp(command, "DRIVE") == 0) {
        if (argc < 3) {
            printf("Error: Missing argument. Use \"/?\" to display help.\n");
            return EXIT_MISSING_ARG;
        }
        const char * arg = argv[2];
        if (strcasecmp(arg, "LIST") == 0) {
            drive_list(data);
            return EXIT_OK;
        }
        if (strcasecmp(arg, "INFO") == 0) {
            if (argc < 4) {
                printf("Error: Missing drive argument. Use \"/?\" to display help.\n");
                return EXIT_MISSING_ARG;
            }
            const uint8_t drive_no = toupper(argv[3][0]) - 'A';
            if (drive_no >= MAX_DRIVES_COUNT) {
                printf("Error: Bad local drive letter\n");
                return EXIT_BAD_DRIVE_LETTER;
            }
            return drive_info(data, drive_no);
        }
        printf("Error: Unknown argument: %s\n", arg);
        return EXIT_UNKNOWN_ARG;
    }

    printf("Error: Unknown command: %s\n", argv[1]);
    return EXIT_UNKNOWN_CMD;
}
