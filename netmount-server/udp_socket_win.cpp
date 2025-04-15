// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#include "udp_socket.hpp"

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <stdexcept>

// Microsoft example says: Need to link with Ws2_32.lib
#pragma comment(lib, "Ws2_32.lib")

class UdpSocket::Impl {
public:
    Impl() {
        WORD version_requested = MAKEWORD(2, 2);
        WSADATA wsa_data;
        if (auto err = WSAStartup(version_requested, &wsa_data); err != NO_ERROR) {
            throw_error("UdpSocket: WSAStartup()", err);
        }

        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) {
            auto err = WSAGetLastError();
            WSACleanup();
            throw_error("UdpSocket: socket()", err);
        }
    }

    ~Impl() {
        signal_stop();
        WSACleanup();
    }

    void bind(const char * local_ip, uint16_t local_port) {
        auto addr = INADDR_ANY;
        if (local_ip && local_ip[0] != '\0') {
            if (inet_pton(AF_INET, local_ip, &addr) <= 0) {
                throw_error("UdpSocket::bind: inet_pton()", WSAGetLastError());
            }
        }

        sockaddr_in bind_addr{};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_addr.s_addr = addr;
        bind_addr.sin_port = htons(local_port);

        if (::bind(sock, reinterpret_cast<const sockaddr *>(&bind_addr), sizeof(bind_addr)) == SOCKET_ERROR) {
            throw_error("UdpSocket::bind: bind()", WSAGetLastError());
        }
    }

    WaitResult wait_for_data(uint16_t timeout_ms) {
        timeval timeout;
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = static_cast<uint32_t>(timeout_ms % 1000) * 1000;

        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(sock, &read_set);

        const auto select_ret = select(0, &read_set, NULL, NULL, &timeout);
        if (select_ret == SOCKET_ERROR) {
            throw_error("UdpSocket::wait_for_data: select()", WSAGetLastError());
        }

        if (signaled.test()) {
            return WaitResult::SIGNAL;
        }

        if (select_ret == 0) {
            return WaitResult::TIMEOUT;
        }

        return WaitResult::READY;
    }

    uint16_t receive(void * buffer, size_t bufferSize) {
        socklen_t addr_len = sizeof(last_remote_addr);
        const auto bytes_received = recvfrom(
            sock,
            reinterpret_cast<char *>(buffer),
            static_cast<int>(bufferSize),
            0,
            reinterpret_cast<sockaddr *>(&last_remote_addr),
            &addr_len);

        if (bytes_received == SOCKET_ERROR) {
            if (signaled.test()) {
                throw_error("UdpSocket::receive: recvfrom(): Stop signal caught");
            }
            throw_error("UdpSocket::receieve: recvfrom()", WSAGetLastError());
        }

        return bytes_received;
    }

    uint16_t send_reply(const void * data, size_t dataSize) {
        const auto sent_bytes = sendto(
            sock,
            reinterpret_cast<const char *>(data),
            static_cast<int>(dataSize),
            0,
            reinterpret_cast<const sockaddr *>(&last_remote_addr),
            sizeof(last_remote_addr));

        if (sent_bytes == SOCKET_ERROR) {
            if (signaled.test()) {
                throw_error("UdpSocket::send_reply: sendto(): Stop signal caught");
            }
            throw_error("UdpSocket::send_reply: sendto()", WSAGetLastError());
        }

        return sent_bytes;
    }

    uint32_t get_last_remote_ip() const { return ntohl(last_remote_addr.sin_addr.s_addr); }

    const std::string & get_last_remote_ip_str() const {
        char ipStr[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &last_remote_addr.sin_addr, ipStr, sizeof(ipStr))) {
            throw_error("UdpSocket::get_last_remote_ip_str: inet_ntop()", WSAGetLastError());
        }
        last_remote_ip = ipStr;
        return last_remote_ip;
    }

    uint16_t get_last_remote_port() const { return ntohs(last_remote_addr.sin_port); }

    void signal_stop() {
        if (signaled.test_and_set()) {
            return;
        }
        closesocket(sock);
        sock = INVALID_SOCKET;
    }

private:
    SOCKET sock;
    sockaddr_in last_remote_addr{};
    mutable std::string last_remote_ip{};
    std::atomic_flag signaled{};

    static std::string get_WSA_error_message(int error_code) {
        char * msg_buffer = nullptr;
        FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            error_code,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&msg_buffer,
            0,
            nullptr);
        std::string message = msg_buffer ? msg_buffer : "Unknown error";
        LocalFree(msg_buffer);
        return message;
    }

    [[noreturn]] static void throw_error(const std::string & context, int error_code) {
        throw std::runtime_error(context + ": " + get_WSA_error_message(error_code));
    }

    [[noreturn]] static void throw_error(const std::string & message) { throw std::runtime_error(message); }
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

void UdpSocket::signal_stop() { p_impl->signal_stop(); }
