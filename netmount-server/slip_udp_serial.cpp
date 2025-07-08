// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#include "slip_udp_serial.hpp"

#include "logger.hpp"
#include "utils.hpp"

#include <cstring>
#include <format>
#include <stdexcept>

namespace {

constexpr uint16_t MTU = 1500;

constexpr uint8_t SLIP_END = 0xC0;
constexpr uint8_t SLIP_ESC = 0xDB;
constexpr uint8_t SLIP_ESC_END = 0xDC;
constexpr uint8_t SLIP_ESC_ESC = 0xDD;

constexpr uint8_t IPV4_PROTOCOL_UDP = 17;

// structs are packed
#pragma pack(push, 1)

union ipv4_addr {
    uint8_t bytes[4];
    uint32_t value;  // value in network byte order
};

struct ipv4_hdr {
    uint8_t version_ihl;  // version:4 | ihl:4
    uint8_t tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag_offset;  // flags:3 | frag_ffset:13
    uint8_t ttl;
    uint8_t protocol;
    uint16_t hdr_csum;
    union ipv4_addr src_addr;
    union ipv4_addr dst_addr;
};

struct udp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
};

struct net_headers {
    ipv4_hdr ipv4;
    udp_hdr udp;
};

#pragma pack(pop)

// Compute Internet Checksum for "len" bytes beginning at location "addr".
// Taken from https://tools.ietf.org/html/rfc1071
uint16_t internet_checksum(const void * addr, uint16_t len) {
    uint32_t sum = 0;
    auto * ptr = static_cast<const uint16_t *>(addr);

    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }

    //  Add left-over byte, if any
    if (len > 0) {
        sum += *reinterpret_cast<const uint8_t *>(ptr);
    }

    //  Fold 32-bit sum to 16 bits
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }

    return ~sum;
}


void build_headers(
    net_headers & headers,
    std::uint16_t & last_sent_packet_id,
    std::uint32_t src_ip,
    std::uint32_t dst_ip,
    std::uint16_t src_port,
    std::uint16_t dst_port,
    size_t length) {

    headers.ipv4.version_ihl = (4 << 4) | (sizeof(ipv4_hdr) / 4);
    headers.ipv4.tos = 0;
    headers.ipv4.total_len = to_big16(sizeof(net_headers) + length);
    headers.ipv4.id = to_big16(++last_sent_packet_id);
    headers.ipv4.flags_frag_offset =
        to_big16(0x2 << 13);  // flags = 0x2 (3 bits, 0x2 = don't fragment); frag_offset = 0
    headers.ipv4.ttl = 64;
    headers.ipv4.protocol = IPV4_PROTOCOL_UDP;
    headers.ipv4.hdr_csum = 0;  // 0 is used during computing header csum, then replaced by computed value
    headers.ipv4.src_addr.value = to_big32(src_ip);
    headers.ipv4.dst_addr.value = to_big32(dst_ip);

    // The IP header checksum is mandatory. It must always be sent.
    headers.ipv4.hdr_csum = internet_checksum(&headers.ipv4, sizeof(ipv4_hdr));

    headers.udp.src_port = to_big16(src_port);
    headers.udp.dst_port = to_big16(dst_port);
    headers.udp.length = to_big16(sizeof(udp_hdr) + length);
    headers.udp.checksum = 0;  // 0 - not used
}


size_t encode_slip_block(std::uint8_t * dest, const void * data, std::size_t length) {
    size_t dest_len = 0;

    auto * data_bytes = reinterpret_cast<const std::uint8_t *>(data);
    auto * data_bytes_end = data_bytes + length;
    while (data_bytes < data_bytes_end) {
        const auto data_byte = *data_bytes++;
        if (data_byte == SLIP_END) {
            dest[dest_len++] = SLIP_ESC;
            dest[dest_len++] = SLIP_ESC_END;
        } else if (data_byte == SLIP_ESC) {
            dest[dest_len++] = SLIP_ESC;
            dest[dest_len++] = SLIP_ESC_ESC;
        } else {
            dest[dest_len++] = data_byte;
        }
    }

    return dest_len;
}


size_t encode_slip(
    std::vector<std::uint8_t> & tx_buffer, const net_headers & headers, const void * data, std::size_t length) {
    size_t tx_len = 0;

    tx_buffer[tx_len++] = SLIP_END;
    tx_len += encode_slip_block(tx_buffer.data() + tx_len, &headers, sizeof(headers));
    tx_len += encode_slip_block(tx_buffer.data() + tx_len, data, length);
    tx_buffer[tx_len++] = SLIP_END;

    return tx_len;
}

}  // namespace


SlipUdpSerial::SlipUdpSerial(const std::string & device) : serial{device} {
    rx_buffer.resize(MTU);
    tx_buffer.resize(MTU * 2);
}

SlipUdpSerial::~SlipUdpSerial() = default;


void SlipUdpSerial::setup(unsigned int baudrate, bool hw_flow_control) { serial.setup(baudrate, hw_flow_control); }


void SlipUdpSerial::send(
    std::uint32_t src_ip,
    std::uint32_t dst_ip,
    std::uint16_t src_port,
    std::uint16_t dst_port,
    const void * data,
    std::size_t length) {
    if (length > MTU) {
        throw std::runtime_error(std::format("SlipUdpSerial::send: Data length is bigger than MTU ({})", MTU));
    }
    net_headers headers;
    build_headers(headers, last_sent_packet_id, src_ip, dst_ip, src_port, dst_port, length);
    const auto tx_buffer_length = encode_slip(tx_buffer, headers, data, length);
    const auto written = serial.write_bytes(tx_buffer.data(), tx_buffer_length);
    log(LogLevel::DEBUG,
        "SlipUdpSerial::send() request to write {} bytes, written {} bytes\n",
        tx_buffer_length,
        written);
    if (written != static_cast<int>(tx_buffer_length)) {
        throw std::runtime_error("SlipUdpSerial::send: Sent a different number of bytes than requested");
    }
}


void SlipUdpSerial::send_reply(const void * data, std::size_t length) {
    send(last_dst_ip, last_remote_ip, last_dst_port, last_remote_port, data, length);
}


std::uint16_t SlipUdpSerial::receive() {
    const auto rx_length = recv_decode_slip();
    if (rx_length == 0) {
        return 0;
    }
    return parse_udp_packet(rx_length);
}

const void * SlipUdpSerial::get_last_rx_data() const { return last_rx_udp_data; }

std::uint16_t SlipUdpSerial::get_last_rx_data_len() const { return last_udp_data_len; }

std::uint32_t SlipUdpSerial::get_last_remote_ip() const { return last_remote_ip; }

const std::string & SlipUdpSerial::get_last_remote_ip_str() const { return last_remote_ip_str; }

std::uint16_t SlipUdpSerial::get_last_remote_port() const { return last_remote_port; }

std::uint32_t SlipUdpSerial::get_last_dst_ip() const { return last_dst_ip; }

const std::string & SlipUdpSerial::get_last_dst_ip_str() const { return last_dst_ip_str; }

std::uint16_t SlipUdpSerial::get_last_dst_port() const { return last_dst_port; }


uint16_t SlipUdpSerial::recv_decode_slip() {
    uint16_t len = 0;

    bool started = false;
    uint8_t rcv_byte;
    while (serial.read_byte(rcv_byte) == 1) {
        if (rcv_byte == SLIP_END) {
            log(LogLevel::DEBUG,
                "SlipUdpSerial::recv_decode_slip: recv_decode_slip: Receive SLIP_END: len = {}\n",
                len);
            if (started && len > 0) {
                break;
            } else {
                started = true;
                continue;
            }
        }

        if (!started) {
            log(LogLevel::DEBUG,
                "SlipUdpSerial::recv_decode_slip: Received character ignored, waiting for SLIP_END character\n");
            continue;
        }

        if (len + 1U == rx_buffer.size()) {
            log(LogLevel::ERROR,
                "SlipUdpSerial::recv_decode_slip: Received data length bigger than buffer size (MTU = {})\n",
                MTU);
            return 0;
        }

        if (rcv_byte == SLIP_ESC) {
            // escape character, read next byte
            if (serial.read_byte(rcv_byte) != 1) {
                break;
            }
            if (rcv_byte == SLIP_ESC_END) {
                rx_buffer[len++] = SLIP_END;
            } else if (rcv_byte == SLIP_ESC_ESC) {
                rx_buffer[len++] = SLIP_ESC;
            }
        } else {
            rx_buffer[len++] = rcv_byte;
        }
    }

    return len;
}


std::uint16_t SlipUdpSerial::parse_udp_packet(std::uint16_t rx_packet_len) {
    if (rx_packet_len < sizeof(net_headers)) {
        log(LogLevel::INFO, "SlipUdpSerial::parse_udp_packet: Short datagram received\n");
        return 0;
    }

    auto * headers = reinterpret_cast<const net_headers *>(rx_buffer.data());

    if ((headers->ipv4.version_ihl & 0xF0) != (4 << 4)) {
        log(LogLevel::NOTICE, "SlipUdpSerial::parse_udp_packet: Received datagram is not a IPv4 packet\n");
        return 0;
    }
    if ((headers->ipv4.version_ihl & 0x0F) != (sizeof(ipv4_hdr) / 4)) {
        log(LogLevel::WARNING,
            "SlipUdpSerial::parse_udp_packet: Received datagram has unsupported IPv4 header length\n");
        return 0;
    }

    // The IP header checksum is mandatory. It must always be sent.
    if (internet_checksum(&headers->ipv4, sizeof(ipv4_hdr)) != 0) {
        log(LogLevel::WARNING,
            "SlipUdpSerial::parse_udp_packet: Received datagram has an invalid IPv4 header checksum\n");
        return 0;
    }

    if (headers->ipv4.protocol != IPV4_PROTOCOL_UDP) {
        log(LogLevel::DEBUG, "SlipUdpSerial::parse_udp_packet: Received datagram is not a UDP packet\n");
        return 0;
    }

    if (rx_packet_len < from_big16(headers->ipv4.total_len)) {
        log(LogLevel::WARNING,
            "SlipUdpSerial::parse_udp_packet: Corrupted datagram received, length shorter than ipv4.total_len\n");
        return 0;
    }

    last_remote_ip = from_big32(headers->ipv4.src_addr.value);
    last_dst_ip = from_big32(headers->ipv4.dst_addr.value);

    last_remote_port = from_big16(headers->udp.src_port);
    last_dst_port = from_big16(headers->udp.dst_port);
    const uint16_t udp_len = from_big16(headers->udp.length);
    // headers->udp.checksum is ignored

    if (udp_len < sizeof(udp_hdr)) {
        log(LogLevel::WARNING, "SlipUdpSerial::parse_udp_packet: Corrupted datagram received, short udp.length\n");
        return 0;
    }

    if (rx_packet_len < sizeof(ipv4_hdr) + udp_len) {
        log(LogLevel::WARNING,
            "SlipUdpSerial::parse_udp_packet: Corrupted datagram received, length shorter than udp.length plus IPv4 "
            "header length\n");
        return 0;
    }

    auto & remote_ip = headers->ipv4.src_addr.bytes;
    last_remote_ip_str = std::format("{:d}.{:d}.{:d}.{:d}", remote_ip[0], remote_ip[1], remote_ip[2], remote_ip[3]);

    auto & dst_ip = headers->ipv4.dst_addr.bytes;
    last_dst_ip_str = std::format("{:d}.{:d}.{:d}.{:d}", dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);

    const std::uint16_t data_len = udp_len - sizeof(udp_hdr);
    last_rx_udp_data = rx_buffer.data() + sizeof(net_headers);
    last_udp_data_len = data_len;
    return data_len;
}
