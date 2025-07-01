// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#include "serial_port.hpp"

#include <windows.h>

#include <stdexcept>

class SerialPort::Impl {
public:
    Impl(const std::string & device) {
        std::string port_name = "\\\\.\\" + device;

        h_serial = CreateFileA(port_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h_serial == INVALID_HANDLE_VALUE) {
            throw_error("SerialPort: CreateFileA()", GetLastError());
        }
    }

    ~Impl() { CloseHandle(h_serial); }

    void setup(std::uint32_t baudrate, bool hw_flow_control) {
        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);

        if (GetCommState(h_serial, &dcb) == 0) {
            throw_error("SerialPort::setup: GetCommState()", GetLastError());
        }

        dcb.BaudRate = baudrate;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fRtsControl = hw_flow_control ? RTS_CONTROL_HANDSHAKE : RTS_CONTROL_DISABLE;

        if (!SetCommState(h_serial, &dcb)) {
            throw_error("SerialPort::setup: SetCommState()", GetLastError());
        }

        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = 0;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.ReadTotalTimeoutConstant = 1000;
        timeouts.WriteTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 0;
        SetCommTimeouts(h_serial, &timeouts);
    }

    ssize_t read_byte(uint8_t & byte) {
        DWORD bytes_read = 0;
        if (ReadFile(h_serial, &byte, 1, &bytes_read, nullptr) == 0) {
            throw_error("SerialPort: ReadFile()", GetLastError());
        }
        return bytes_read;
    }

    ssize_t write_bytes(const uint8_t * data, size_t size) {
        DWORD bytes_written = 0;
        if (WriteFile(h_serial, data, static_cast<DWORD>(size), &bytes_written, nullptr) == 0) {
            throw_error("SerialPort: ReadFile()", GetLastError());
        }
        return bytes_written;
    }

private:
    HANDLE h_serial{INVALID_HANDLE_VALUE};

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
};


SerialPort::SerialPort(const std::string & device) : p_impl(new Impl(device)) {}

SerialPort::~SerialPort() = default;

void SerialPort::setup(std::uint32_t baudrate, bool hw_flow_control) { p_impl->setup(baudrate, hw_flow_control); }

ssize_t SerialPort::read_byte(std::uint8_t & byte) { return p_impl->read_byte(byte); }

ssize_t SerialPort::write_bytes(const std::uint8_t * data, size_t size) { return p_impl->write_bytes(data, size); }
