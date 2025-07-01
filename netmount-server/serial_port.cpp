// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#include "serial_port.hpp"

#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <stdexcept>

class SerialPort::Impl {
public:
    Impl(const std::string & device) {
        fd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
        if (fd == INVALID_FD) {
            throw_error("SerialPort: open()", errno);
        }
    }

    ~Impl() { ::close(fd); }

    void setup(unsigned int baudrate, bool hw_flow_control) {
        struct termios tty{};
        if (tcgetattr(fd, &tty) != 0) {
            throw_error("SerialPort::setup: tcgetattr()", errno);
        }

        speed_t speed;
        switch (baudrate) {
            case 1200:
                speed = B1200;
                break;
            case 2400:
                speed = B2400;
                break;
            case 4800:
                speed = B4800;
                break;
            case 9600:
                speed = B9600;
                break;
            case 19200:
                speed = B19200;
                break;
            case 38400:
                speed = B38400;
                break;
            case 57600:
                speed = B57600;
                break;
            case 115200:
                speed = B115200;
                break;
            case 230400:
                speed = B230400;
                break;
            default:
                throw std::runtime_error("SerialPort::setup: Unsupported baudrate: " + std::to_string(baudrate));
        }

        cfsetospeed(&tty, speed);
        cfsetispeed(&tty, speed);

        tty.c_cflag |= (CLOCAL | CREAD);
        tty.c_cflag &= ~CSIZE;
        tty.c_cflag |= CS8;
        tty.c_cflag &= ~(PARENB | PARODD);  // shut off parity
        tty.c_cflag &= ~CSTOPB;

        if (hw_flow_control)
            tty.c_cflag |= CRTSCTS;
        else
            tty.c_cflag &= ~CRTSCTS;

        tty.c_lflag = 0;  // no signaling chars, no echo

        tty.c_iflag &= ~(IXON | IXOFF | IXANY);  // turn off xon/xoff flow ctrl
        tty.c_iflag &=
            ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
              ICRNL);  // Disable any special handling of received bytes

        // no canonical processing
        tty.c_oflag = 0;  // no remapping, no delays

        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 10;

        if (tcsetattr(fd, TCSANOW, &tty) != 0) {
            throw_error("SerialPort::setup: tcsetattr()", errno);
        }
    }

    ssize_t read_byte(uint8_t & byte) {
        const auto bytes_read = ::read(fd, &byte, 1);
        if (bytes_read == -1) {
            throw_error("SerialPort::read_byte: read()", errno);
        }
        return bytes_read;
    }

    ssize_t write_bytes(const uint8_t * data, size_t size) {
        const auto bytes_written = ::write(fd, data, size);
        if (bytes_written == -1) {
            throw_error("SerialPort::write_bytes: write()", errno);
        }

        return bytes_written;
    }

private:
    static constexpr int INVALID_FD = -1;
    int fd{INVALID_FD};

    [[noreturn]] static void throw_error(const std::string & context, int error_code) {
        throw std::runtime_error(context + ": " + strerror(error_code));
    }
};


SerialPort::SerialPort(const std::string & device) : p_impl(new Impl(device)) {}

SerialPort::~SerialPort() = default;

void SerialPort::setup(std::uint32_t baudrate, bool hw_flow_control) { p_impl->setup(baudrate, hw_flow_control); }

ssize_t SerialPort::read_byte(std::uint8_t & byte) { return p_impl->read_byte(byte); }

ssize_t SerialPort::write_bytes(const std::uint8_t * data, size_t size) { return p_impl->write_bytes(data, size); }
