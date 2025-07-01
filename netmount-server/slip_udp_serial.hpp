// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

// Implements IP, UDP and SLIP protokols over serial line.

#ifndef _SLIP_UDP_SERIAL_HPP_
#define _SLIP_UDP_SERIAL_HPP_

#include "serial_port.hpp"

#include <cstdint>
#include <string>
#include <vector>

class SlipUdpSerial {
public:
    SlipUdpSerial(const std::string & device);
    ~SlipUdpSerial();

    void setup(unsigned int baudrate, bool hw_flow_control);

    void send(
        std::uint32_t src_ip,
        std::uint32_t dst_ip,
        std::uint16_t src_port,
        std::uint16_t dst_port,
        const void * data,
        std::size_t length);

    void send_reply(const void * data, std::size_t length);

    std::uint16_t receive();

    const void * get_last_rx_data() const;
    std::uint16_t get_last_rx_data_len() const;

    std::uint32_t get_last_remote_ip() const;
    const std::string & get_last_remote_ip_str() const;
    std::uint16_t get_last_remote_port() const;

    std::uint32_t get_last_dst_ip() const;
    const std::string & get_last_dst_ip_str() const;
    std::uint16_t get_last_dst_port() const;

private:
    SerialPort serial;
    std::vector<std::uint8_t> rx_buffer;
    std::uint16_t last_udp_data_len;
    const void * last_rx_udp_data{nullptr};

    std::vector<std::uint8_t> tx_buffer;

    std::uint32_t last_remote_ip;
    std::uint16_t last_remote_port;
    std::string last_remote_ip_str;

    // Destination IP address and UDP port of last received packet
    std::uint32_t last_dst_ip;
    std::uint16_t last_dst_port;
    std::string last_dst_ip_str;

    std::uint16_t last_sent_packet_id{0};

    std::uint16_t recv_decode_slip();
    std::uint16_t parse_udp_packet(std::uint16_t rx_packet_len);
};

#endif
