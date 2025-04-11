// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#ifndef _UDP_SOCKET_HPP_
#define _UDP_SOCKET_HPP_

#include <cstdint>
#include <memory>
#include <string>

class UdpSocket {
public:
    enum class WaitResult { TIMEOUT, READY, SIGNAL };

    UdpSocket();
    ~UdpSocket();

    void bind(const char * local_ip, std::uint16_t local_port);
    WaitResult wait_for_data(std::uint16_t timeout_ms);
    WaitResult can_send_data(std::uint16_t timeout_ms);
    std::uint16_t receive(void * buffer, size_t buffer_size);
    std::uint16_t send_reply(const void * buffer, size_t data_len);
    std::uint32_t get_last_remote_ip() const;
    const std::string & get_last_remote_ip_str() const;
    std::uint16_t get_last_remote_port() const;

private:
    class Impl;
    std::unique_ptr<Impl> p_impl;
};

#endif
