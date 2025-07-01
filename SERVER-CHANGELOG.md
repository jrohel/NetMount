# 1.3.0

## Features

- **Integrate SLIP support (implement IP, UDP, and SLIP protocols)**

    There are cases where having SLIP support built directly into
    netmount-server is useful. For example:

    - The operating system does not support SLIP
    - The user does not have permission to configure the system network
    - The user wants to keep the SLIP client fully isolated from the system
      and other networks (preventing possible IP address conflicts between
      SLIP and other networks)
    - The user prefers not to modify the system’s network configuration
    - The user wants to quickly and easily run netmount-server over SLIP

    This implementation adds new arguments:

    `--slip-dev=<SERIAL_DEVICE>`  Serial device used for SLIP (host network
                                  is used by default)

    `--slip-speed=<SERIAL_SPEED>` Baud rate of the SLIP serial device

    `--slip-rts-cts=<ENABLED>`    Enable hardware flow control: 0 = OFF,
                                  1 = ON (default: OFF)

    By default, netmount-server still uses the operating system’s network
    stack. When the `--slip-dev=<SERIAL_DEVICE>` option is used, it switches
    to the internal SLIP implementation and shares data via the specified
    `SERIAL_DEVICE`. In this case, the `--slip-speed=<BAUD_RATE>` option is
    required to set the baud rate of the serial device. Optionally, hardware
    flow control can be enabled using `--slip-rts-cts=1`.

    The MTU of the built-in implementation is 1500 bytes - meaning
    the maximum size of a received and transmitted IP packet is 1500 bytes.
    To use a smaller MTU, configure it on the netmount DOS
    client side. The client will send and request packets accordingly.

    The `--bind-addr=<IP_ADDR>` option is not supported in this mode.
    netmount-server responds to all IP addresses, and the source IP address
    in reply matches the destination address of the incoming request.
    UDP port handling works the same as when using the operating system’s
    network stack - by default, netmount-server listens on port 12200, which
    can be changed using the `--bind-port=<UDP_PORT>` option.

    The internal implementation is completely isolated from the system’s
    network. The only requirement is user access to the serial port.

    Example usage:

    `netmount-server --slip-dev=/dev/ttyUSB0 --slip-speed=115200 C=/shared/`

- **Remove shared FilesystemDB, each drive has its own handle table**

    Previously, all shared drives used a single items table. Since DOS uses
    a 16-bit number to store the start cluster (which we use as a handle),
    this limited the total number of entries across all shared drives to
    65,535.

    **Now, each shared drive has its own table, allowing up to 65,535 entries
    per drive.**

    Additionally, it's now possible to share the same directory multiple
    times with different settings — in particular, with different
    `name_conversion` configurations.

## Other

- Optimize: Dynamically allocate slots for handles: Previously, memory for all handles
  (65,535 slots) was allocated at program startup. Now, the slot space grows dynamically
  as needed

----

# 1.2.0

## Features

- Server file names conversion to DOS 8.3 format

    The server implements its own conversion of existing file names to DOS short names 8.3.
    The advantage of the conversion is that it is independent of the operating system and file system.
    The disadvantage is that the mapping of converted file names to existing ones is only temporary
    in RAM and is unstable (re-created during some operations).

    Added optional argument `name_conversion=<method>` to the shared drive definition.
    Supported values for `<method>` are `RAM` and `OFF`. The default is `RAM`.
    The value `OFF` disables file name conversion.

## Fixes

- Update FilesystemDB::Item::last_used_time - Previously, `FilesystemDB::Item::last_used_time`
  was updated only in the `FilesystemDB::get_handle` method. This ignored client accesses made
  directly via the handle.

- FilesystemDB::Item::create_directory_list store only 65,535 items - This is due to the DOS FIND
  function using a 16-bit offset, which cannot address more entries.

## Other

- Added Makefile.cross: Example of Makefile for cross-compilation

- Optimize FilesystemDB::find_file: The loop now starts from the requested offset

----

# 1.1.0

## Features

- **Supports POSIX-compliant operating systems (Linux, *BSD, macOS, etc.) and Microsoft Windows**

    Previously only Linux was supported.

    - Linux `ioctl` is now used only in Linux build (for FAT attributes)
    - Implemented a new `UdpSocket` class, implemented for POSIX environments and Microsoft Windows
    - Implemented platform-independent byte order conversion functions (little-endian / big-endian)
    - Use `std::filesystem::space` instead of `statvfs`
    - Use `std::filesystem` for make/delete/change directory
    - Use `std::filesystem` for iterate directory and delete file
    - Use `std::filesystem::path::string` instead of `path::native`
    - Added Makefile.windows to build for Windows using MinGW

## Fixes

- Prevent appending an empty search template to the parent path

## Other

- Marked many variables as `const`

----

# 1.0.2

## Fixes

- open file: handle errors (exceptions) when testing directory existence

- Directory creation now respects umask

    Previously, directories were created with mode 0000, granting no permissions to anyone,
    which is typically not useful. Now, directories are created with mode 0777, and the effective
    permissions are controlled by the process's umask, just like file creation.

----

# 1.0.1

## Fixes

- Include "algorithm" header file

## Other

- More "path" and "handle" existence checks

----

# 1.0.0

- First version
