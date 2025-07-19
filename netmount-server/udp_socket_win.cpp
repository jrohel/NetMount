// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#include "logger.hpp"
#include "udp_socket.hpp"
#include "utils.hpp"

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <stdexcept>

class DynamicLibrary {
public:
    DynamicLibrary() = default;

    ~DynamicLibrary() { unload(); }

    DynamicLibrary(const DynamicLibrary &) = delete;
    DynamicLibrary & operator=(const DynamicLibrary &) = delete;

    DynamicLibrary(DynamicLibrary && other) noexcept {
        handle = other.handle;
        other.handle = nullptr;
    }

    DynamicLibrary & operator=(DynamicLibrary && other) noexcept {
        if (this != &other) {
            unload();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }

    bool load(const char * library_name) noexcept {
        unload();  // Unload any existing library
        handle = LoadLibraryA(library_name);
        return handle;
    }

    void unload() noexcept {
        if (handle) {
            FreeLibrary(handle);
            handle = nullptr;
        }
    }

    FARPROC get_function(const char * function_name) const noexcept {
        if (!handle) {
            return nullptr;
        }

        return GetProcAddress(handle, function_name);
    }

    template <typename T>
    T get_function(const char * function_name) const noexcept {
        union {
            FARPROC raw;
            T typed;
        } caster;

        caster.raw = get_function(function_name);
        return caster.typed;
    }

private:
    HMODULE handle{nullptr};
};


class UdpSocket::Impl {
public:
    Impl() {
        init_library();

        sock = p_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) {
            auto err = p_WSAGetLastError();
            library_cleanup();
            throw_error("UdpSocket: socket()", err);
        }
    }

    ~Impl() {
        signal_stop();
        library_cleanup();
    }

    void bind(const char * local_ip, uint16_t local_port) {
        auto addr = INADDR_ANY;
        if (local_ip && local_ip[0] != '\0') {
            ip_from_string(AF_INET, local_ip, &addr);
        }

        sockaddr_in bind_addr{};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_addr.s_addr = addr;
        bind_addr.sin_port = to_big16(local_port);

        if (p_bind(sock, reinterpret_cast<const sockaddr *>(&bind_addr), sizeof(bind_addr)) == SOCKET_ERROR) {
            throw_error("UdpSocket::bind: bind()", p_WSAGetLastError());
        }
    }

    WaitResult wait_for_data(uint16_t timeout_ms) {
        timeval timeout;
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = static_cast<uint32_t>(timeout_ms % 1000) * 1000;

        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(sock, &read_set);

        const auto select_ret = p_select(0, &read_set, NULL, NULL, &timeout);
        if (select_ret == SOCKET_ERROR) {
            throw_error("UdpSocket::wait_for_data: select()", p_WSAGetLastError());
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
        const auto bytes_received = p_recvfrom(
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
            throw_error("UdpSocket::receieve: recvfrom()", p_WSAGetLastError());
        }

        return bytes_received;
    }

    uint16_t send_reply(const void * data, size_t dataSize) {
        const auto sent_bytes = p_sendto(
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
            throw_error("UdpSocket::send_reply: sendto()", p_WSAGetLastError());
        }

        return sent_bytes;
    }

    uint32_t get_last_remote_ip() const { return from_big32(last_remote_addr.sin_addr.s_addr); }

    const std::string & get_last_remote_ip_str() const {
        char ipStr[INET_ADDRSTRLEN];
        ip_to_string(AF_INET, &last_remote_addr.sin_addr, ipStr, sizeof(ipStr));
        last_remote_ip = ipStr;
        return last_remote_ip;
    }

    uint16_t get_last_remote_port() const { return from_big16(last_remote_addr.sin_port); }

    void signal_stop() {
        if (signaled.test_and_set()) {
            return;
        }
        p_closesocket(sock);
        sock = INVALID_SOCKET;
    }

private:
    DynamicLibrary ws2_lib;
    bool loaded{false};

    INT(WSAAPI * p_WSAStartup)(WORD wVersionRequested, LPWSADATA lpWSAData) = nullptr;
    INT(WSAAPI * p_WSACleanup)(VOID) = nullptr;
    INT(WSAAPI * p_WSAGetLastError)(VOID) = nullptr;

    SOCKET(WSAAPI * p_socket)(INT af, INT type, INT protocol) = nullptr;
    INT(WSAAPI * p_closesocket)(SOCKET s) = nullptr;

    INT(WSAAPI * p_bind)(SOCKET s, const struct sockaddr * name, INT namelen) = nullptr;

    INT(WSAAPI * p_recvfrom)(SOCKET s, CHAR * buf, INT len, INT flags, struct sockaddr * from, INT * fromlen) = nullptr;
    INT(WSAAPI * p_sendto)(SOCKET s, const CHAR * buf, INT len, INT flags, const struct sockaddr * to, INT tolen) =
        nullptr;

    INT(WSAAPI * p_select)(
        INT nfds, fd_set * readfds, fd_set * writefds, fd_set * exceptfds, const struct timeval * timeout) = nullptr;

    PCSTR(WSAAPI * p_inet_ntop)(INT Family, const VOID * pAddr, PSTR pStringBuf, size_t StringBufSize) = nullptr;
    INT(WSAAPI * p_inet_pton)(INT Family, PCSTR pszAddrString, PVOID pAddrBuf) = nullptr;

    unsigned long(WSAAPI * p_inet_addr)(const char * cp) = nullptr;
    char *(WSAAPI * p_inet_ntoa)(in_addr in) = nullptr;

    SOCKET sock;
    sockaddr_in last_remote_addr{};
    mutable std::string last_remote_ip{};
    std::atomic_flag signaled{};

    static std::string get_error_message(int error_code) {
        char * msg_buffer = nullptr;
        FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            error_code,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&msg_buffer,
            0,
            nullptr);
        const std::string message = msg_buffer ? msg_buffer : "Unknown error";
        LocalFree(msg_buffer);
        return message;
    }

    [[noreturn]] static void throw_error(const std::string & context, int error_code) {
        throw std::runtime_error(context + ": " + get_error_message(error_code));
    }

    [[noreturn]] static void throw_error(const std::string & message) { throw std::runtime_error(message); }

    void init_library() {
        if (loaded) {
            return;
        }

        if (!ws2_lib.load("ws2_32.dll")) {
            throw_error("UdpSocket::init_library: load(\"ws2_32.dll\")", GetLastError());
        }

        if (!(p_WSAStartup = ws2_lib.get_function<decltype(p_WSAStartup)>("WSAStartup"))) {
            throw_error("UdpSocket::init_library: get_function(\"WSAStartup\")", GetLastError());
        }
        if (!(p_WSACleanup = ws2_lib.get_function<decltype(p_WSACleanup)>("WSACleanup"))) {
            throw_error("UdpSocket::init_library: get_function(\"WSACleanup\")", GetLastError());
        }
        if (!(p_WSAGetLastError = ws2_lib.get_function<decltype(p_WSAGetLastError)>("WSAGetLastError"))) {
            throw_error("UdpSocket::init_library: get_function(\"WSAGetLastError\")", GetLastError());
        }

        if (!(p_socket = ws2_lib.get_function<decltype(p_socket)>("socket"))) {
            throw_error("UdpSocket::init_library: get_function(\"socket\")", GetLastError());
        }
        if (!(p_closesocket = ws2_lib.get_function<decltype(p_closesocket)>("closesocket"))) {
            throw_error("UdpSocket::init_library: get_function(\"closesocket\")", GetLastError());
        }

        if (!(p_bind = ws2_lib.get_function<decltype(p_bind)>("bind"))) {
            throw_error("UdpSocket::init_library: get_function(\"bind\")", GetLastError());
        }

        if (!(p_recvfrom = ws2_lib.get_function<decltype(p_recvfrom)>("recvfrom"))) {
            throw_error("UdpSocket::init_library: get_function(\"recvfrom\")", GetLastError());
        }
        if (!(p_sendto = ws2_lib.get_function<decltype(p_sendto)>("sendto"))) {
            throw_error("UdpSocket::init_library: get_function(\"sendto\")", GetLastError());
        }

        if (!(p_select = ws2_lib.get_function<decltype(p_select)>("select"))) {
            throw_error("UdpSocket::init_library: get_function(\"select\")", GetLastError());
        }

        if (!(p_inet_ntop = ws2_lib.get_function<decltype(p_inet_ntop)>("inet_ntop"))) {
            log(LogLevel::NOTICE,
                "UdpSocket::init_library: get_function(\"inet_ntop\"): {}\n",
                get_error_message(GetLastError()));
            log(LogLevel::NOTICE, "UdpSocket::init_library: falling back to \"inet_ntoa\"\n");
            if (!(p_inet_ntoa = ws2_lib.get_function<decltype(p_inet_ntoa)>("inet_ntoa"))) {
                throw_error("UdpSocket::init_library: get_function(\"inet_ntoa\")", GetLastError());
            }
        }
        if (!(p_inet_pton = ws2_lib.get_function<decltype(p_inet_pton)>("inet_pton"))) {
            log(LogLevel::NOTICE,
                "UdpSocket::init_library: get_function(\"inet_pton\"): {}\n",
                get_error_message(GetLastError()));
            log(LogLevel::NOTICE, "UdpSocket::init_library: falling back to \"inet_addr\"\n");
            if (!(p_inet_addr = ws2_lib.get_function<decltype(p_inet_addr)>("inet_addr"))) {
                throw_error("UdpSocket::init_library: get_function(\"inet_addr\")", GetLastError());
            }
        }

        WORD version_requested = MAKEWORD(2, 2);
        WSADATA wsa_data;
        if (auto err = p_WSAStartup(version_requested, &wsa_data); err != NO_ERROR) {
            throw_error("UdpSocket: WSAStartup()", err);
        }

        loaded = true;
    }

    void library_cleanup() noexcept {
        if (loaded) {
            p_WSACleanup();
        }
        ws2_lib.unload();
        loaded = false;
    }

    // Converts an IPv4 or IPv6 address from its standard text representation to its numeric binary form.
    // Uses `inet_pton`, with a fallback to `inet_addr` for IPv4 on legacy Windows.
    void ip_from_string(int af, const char * src, void * dst) const {
        if (p_inet_pton) {
            auto ret = p_inet_pton(af, src, dst);
            if (ret == 1) {
                return;
            }
            if (ret == 0) {
                throw_error("UdpSocket::ip_from_string: inet_pton(): Not valid address string");
            } else {
                throw_error("UdpSocket::ip_from_string: inet_pton()", p_WSAGetLastError());
            }
        }

        if (af == AF_INET) {
            // Falling back to the legacy function; only IPv4 is supported.
            struct in_addr addr;
            addr.s_addr = p_inet_addr(src);
            if (addr.s_addr != INADDR_NONE || strcmp(src, "255.255.255.255") == 0) {
                *reinterpret_cast<struct in_addr *>(dst) = addr;
                return;
            }
            throw_error("UdpSocket::ip_from_string: inet_addr(): failed");
        }

        throw_error("UdpSocket::ip_from_string: Unsupported function");
    }

    // Converts an IPv4 or IPv6 network address to a string in standard text format.
    // Uses `inet_ntop`, with a fallback to `inet_ntoa` for IPv4 on legacy Windows.
    void ip_to_string(int af, const void * src, char * dst, socklen_t size) const {
        if (p_inet_ntop) {
            if (p_inet_ntop(af, src, dst, size)) {
                return;
            }
            throw_error("UdpSocket::ip_to_string: inet_ntop()", p_WSAGetLastError());
        }

        if (af == AF_INET) {
            // Falling back to the legacy function; only IPv4 is supported.
            auto addr = *reinterpret_cast<const struct in_addr *>(src);
            const char * ip_string = p_inet_ntoa(addr);
            if (ip_string && dst) {
                strncpy(dst, ip_string, size);
                dst[size - 1] = '\0';
                return;
            }
            throw_error("UdpSocket::ip_to_string: inet_ntoa(): failed");
        }

        throw_error("UdpSocket::ip_to_string: Unsupported function");
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

void UdpSocket::signal_stop() { p_impl->signal_stop(); }
