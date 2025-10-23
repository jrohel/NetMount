// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2024-2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#ifndef _SHDATA_H_
#define _SHDATA_H_

#include "nettypes.h"

#include <stdint.h>

// structs are packed
#pragma pack(push, 1)

typedef void(__interrupt * interrupt_handler)(void);

#define CHECKSUM_IP_HEADER      0x01
#define CHECKSUM_NETMOUNT_PROTO 0x02

#define MAX_DRIVES_COUNT 26
struct shared_data {
    uint8_t ldrv[MAX_DRIVES_COUNT];  // local to remote drives mappings (0=A, 1=B, 2=C, ...);
                                     // must be first in structure, used in assembler
    struct drive_info {
        uint8_t remote_ip_idx;  // index of server ip address in ip_mac_map table
        uint16_t remote_port;
        uint8_t min_rcv_tmo_18_2_ticks_shr_2;  // Minimum response timeout ((sec * 18.2) >> 2, clock 18.2 Hz)
        uint8_t max_rcv_tmo_18_2_ticks_shr_2;  // Maximum response timeout ((sec * 18.2) >> 2, clock 18.2 Hz)
        uint8_t max_request_retries;           // Maximum number of request retries
        uint8_t enabled_checksums;
        uint8_t min_server_read_len;  // Minimum length of data block read from the server
    } drives[MAX_DRIVES_COUNT];
    union ipv4_addr local_ipv4;
    union ipv4_addr net_mask;
    uint16_t local_port;
    int8_t disable_sending_arp_request;  // A non-zero value disables sending ARP requests, replying is still allowed.
    uint8_t gateway_ip_slot;             // index of gateway ip address in ip_mac_map table
    uint16_t interface_mtu;
    struct ip_mac_map {
        union ipv4_addr ip;
        struct mac_addr mac_addr;
    } ip_mac_map[4];
    uint8_t requested_pktdrv_int;  // requested packet driver interrupt handle number (0 - autodetect)

    uint8_t used_pktdrv_int;  // used/found packet driver interrupt handle number
    uint16_t arp_pkthandle;
    uint16_t ipv4_pkthandle;
    struct mac_addr local_mac_addr;

    // Last used remote address. Waiting for a response from it.
    union ipv4_addr last_remote_ip;
    uint16_t last_remote_udp_port;
    volatile uint8_t server_response_received;  // 1 - if response in global_recv_buff

    // Used only for uninstall
    uint16_t psp_segment;
    void * int2F_redirector_offset;
    interrupt_handler orig_INT2F_handler;
    interrupt_handler pktdrv_INT_handler;
};

#define SHARED_DATA_SIZE 319  // It is sizeof(struct shared_data). Must be adjusted if structure size is changed!
// something like static_assert, error during compilation if SHARED_DATA_SIZE != sizeof(struct shared_data)
typedef char st_assert_SHARED_DATA_SIZE[SHARED_DATA_SIZE == sizeof(struct shared_data) ? 1 : -1];

#pragma pack(pop)

#endif
