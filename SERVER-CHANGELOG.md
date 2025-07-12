# 1.4.0 (2025-07-08)

## Features

- **Replace `err_print` and `dbg_print` with a level-based logger**

    Previously, there were only two logging levels: `dbg_print` and `err_print`.
    Additionally, `dbg_print` had to be enabled at compile time.

    Now, both are replaced with a single `log()` function that supports seven
    logging levels: `CRITICAL`, `ERROR`, `WARNING`, `NOTICE`, `INFO`, `DEBUG`, and `TRACE`.
    Users can configure the desired logging verbosity at application startup.

    Log messages are written to standard error (stderr) and are automatically prefixed
    with ISO 8601 timestamps with millisecond precision.

    A new program argument has been added

    `--log-level=<LEVEL>`  Logging verbosity level: `0 = OFF`, `7 = TRACE` (default: `3`)

- **Add support for setting volume label for shared drives**

    Previously, shared drives had no volume label. Now, the user can specify
    a label for each shared drive. If a volume label is not specified,
    the default label "NETMOUNT" is used. To share a drive without a volume
    label, an empty string can be passed: `--label=`.

    A new program option has been added:

    `<label>=<volume_label>`  volume label (first 11 chars used,
                              default: NETMOUNT; use "--label=" to remove)

## Fixes

- **Don't exit on error in Drive::Item::create_directory_list**

    If an error occurs during directory listing (e.g. when a request is made
    to list a directory using a file-type argument), the error is now only
    logged and does not cause the server to exit.

- **Implement basic INT2F_UNLOCK_FILE handler**

    Previously, only the INT2F_LOCK_UNLOCK_FILE function (a client request)
    was handled. INT2F_UNLOCK_FILE is now supported as well, but both
    handlers currently only validate the file handle and log an ERROR if
    the handle is invalid. Actual locking/unlocking is not yet implemented
    and may be added later.

## Other

- **dump_packet to stderr instead of stdout**

- **server: create_server_path: Add path to exception message**

- **Check if the shared path is a directory**

    Previously, this check was not performed, so the user could specify
    a file (socket, device, ...) instead of a directory as the shared path.

- **create_directory_list: Add "." and ".." entries only to non-root directories**

    Previously, "." and ".." entries were added to the list for all directories,
    and the `find_file` function skipped these entries for root directories.

----

# 1.3.0 (2025-07-02)

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
    times with different settings - in particular, with different
    `name_conversion` configurations.

## Other

- Optimize: Dynamically allocate slots for handles: Previously, memory for all handles
  (65,535 slots) was allocated at program startup. Now, the slot space grows dynamically
  as needed

----

# 1.2.0 (2025-05-21)

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

# 1.1.0 (2025-04-24)

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

# 1.0.2 (2025-04-07)

## Fixes

- open file: handle errors (exceptions) when testing directory existence

- Directory creation now respects umask

    Previously, directories were created with mode 0000, granting no permissions to anyone,
    which is typically not useful. Now, directories are created with mode 0777, and the effective
    permissions are controlled by the process's umask, just like file creation.

----

# 1.0.1 (2025-04-07)

## Fixes

- Include "algorithm" header file

## Other

- More "path" and "handle" existence checks

----

# 1.0.0 (2025-04-01)

- First version
