// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#include "udp_socket.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <stdexcept>

class UdpSocket::Impl {
public:
    Impl() {
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == -1) {
            throw_error("UdpSocket: socket()", errno);
        }
    }

    ~Impl() { close(sock); }

    void bind(const char * local_ip, std::uint16_t local_port) {
        auto addr = INADDR_ANY;
        if (local_ip && local_ip[0] != '\0') {
            if (inet_pton(AF_INET, local_ip, &addr) <= 0) {
                throw_error("UdpSocket::bind: inet_pton()", errno);
            }
        }

        sockaddr_in bind_addr{};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_addr.s_addr = addr;
        bind_addr.sin_port = htons(local_port);

        if (::bind(sock, reinterpret_cast<const sockaddr *>(&bind_addr), sizeof(bind_addr)) == -1) {
            throw_error("UdpSocket::bind: bind()", errno);
        }
    }

    WaitResult wait_for_data(std::uint16_t timeout_ms) {
        timeval timeout;
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = static_cast<std::uint32_t>(timeout_ms % 1000) * 1000;

        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(sock, &read_set);

        const auto select_ret = select(sock + 1, &read_set, NULL, NULL, &timeout);

        if (select_ret == -1) {
            if (errno == EINTR) {
                return WaitResult::SIGNAL;
            } else {
                throw_error("UdpSocket::wait_for_data: select()", errno);
            }
        }

        if (select_ret == 0) {
            return WaitResult::TIMEOUT;
        }

        return WaitResult::READY;
    }

    std::uint16_t receive(void * buffer, size_t buffer_size) {
        socklen_t addr_len = sizeof(last_remote_addr);
        auto bytes_received =
            recvfrom(sock, buffer, buffer_size, 0, reinterpret_cast<sockaddr *>(&last_remote_addr), &addr_len);
        if (bytes_received == -1) {
            throw_error("UdpSocket::receieve: recvfrom()", errno);
        }
        return bytes_received;
    }

    std::uint16_t send_reply(const void * buffer, size_t data_len) {
        auto sent_bytes = sendto(
            sock, buffer, data_len, 0, reinterpret_cast<const sockaddr *>(&last_remote_addr), sizeof(last_remote_addr));
        if (sent_bytes == -1) {
            throw_error("UdpSocket::send_reply: sendto()", errno);
        }
        return sent_bytes;
    }

    std::uint32_t get_last_remote_ip() const { return ntohl(last_remote_addr.sin_addr.s_addr); }

    const std::string & get_last_remote_ip_str() const {
        char ip_cstr[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, reinterpret_cast<const void *>(&last_remote_addr.sin_addr), ip_cstr, sizeof(ip_cstr))) {
            throw_error("UdpSocket::get_last_remote_ip_str: inet_ntop()", errno);
        }
        last_remote_ip = ip_cstr;
        return last_remote_ip;
    }

    std::uint16_t get_last_remote_port() const { return ntohs(last_remote_addr.sin_port); }

private:
    int sock;
    sockaddr_in last_remote_addr;
    mutable std::string last_remote_ip;

    [[noreturn]] static void throw_error(const std::string & context, int error_code) {
        throw std::runtime_error(context + ": " + strerror(error_code));
    }
};


UdpSocket::UdpSocket() : p_impl(new Impl) {}

UdpSocket::~UdpSocket() = default;

void UdpSocket::bind(const char * local_ip, uint16_t local_port) { p_impl->bind(local_ip, local_port); }

UdpSocket::WaitResult UdpSocket::wait_for_data(uint16_t timeout_ms) { return p_impl->wait_for_data(timeout_ms); }

uint16_t UdpSocket::receive(void * buffer, size_t buffer_size) { return p_impl->receive(buffer, buffer_size); }

uint16_t UdpSocket::send_reply(const void * buffer, size_t data_len) { return p_impl->send_reply(buffer, data_len); }

std::uint32_t UdpSocket::get_last_remote_ip() const { return p_impl->get_last_remote_ip(); }

const std::string & UdpSocket::get_last_remote_ip_str() const { return p_impl->get_last_remote_ip_str(); }

uint16_t UdpSocket::get_last_remote_port() const { return p_impl->get_last_remote_port(); }

void UdpSocket::signal_stop() {}
