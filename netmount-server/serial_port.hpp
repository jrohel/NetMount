// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Jaroslav Rohel, jaroslav.rohel@gmail.com

#ifndef _SERIAL_PORT_HPP_
#define _SERIAL_PORT_HPP_

#include <cstdint>
#include <memory>
#include <string>

class SerialPort {
public:
    SerialPort(const std::string & device);
    ~SerialPort();

    void setup(std::uint32_t baudrate, bool hw_flow_control);

    /// Reads one byte from the serial port. If no data is received within one second, a timeout occurs.
    /// Returns the number of bytes read.
    /// Throws std::runtime_error exception in case of an error.
    ssize_t read_byte(std::uint8_t & byte);

    /// Writes data to the serial port.
    /// Returns the number of bytes actually written.
    /// Throws std::runtime_error exception in case of an error.
    ssize_t write_bytes(const std::uint8_t * data, size_t size);

private:
    class Impl;
    std::unique_ptr<Impl> p_impl;
};

#endif
