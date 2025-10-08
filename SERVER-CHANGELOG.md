# 1.6.0 (2025-10-12)

## Features

- **Improve server file names conversion to DOS 8.3 format**

    Previously, invalid characters in filenames were removed.

    **The updated logic now attempts to transliterate Unicode characters to ASCII.**
    It removes combining characters (accents, diacritics, etc.) and replaces
    non-ASCII characters with their ASCII equivalents.

    By default, the transliteration mapping table is loaded from the file
    "netmount-u2a.map", but the path can be changed using the `--translit_map_path=<PATH>` argument.
    Setting `<PATH>` to an empty string disables the use of the mapping table.

    The mapping table is user-extensible and can be customized easily.
    If a Unicode character is not found in the mapping table, it is replaced by an underscore '_'.

    Examples:

    | original name      | old conversion | new but missing map file | new conversion     |
    |--------------------|----------------|--------------------------|--------------------|
    |**`data.tar.gz`**   | `DATATA~1.GZ`  | `DATA_TAR.GZ`            | **`DATA_TAR.GZ`**  |
    |**`NašePísně.doc`** | `NAEPSN~1.DOC` | `NA_EP_~1.DOC`           | **`NASEPI~1.DOC`** |
    |**`píseň.txt`**     | `PSE~1.TXT`    | `P_SE_.TXT`              | **`PISEN.TXT`**    |
    |**`история.txt`**   | `~1.TXT`       | `_______.TXT`            | **`ISTORIJA.TXT`** |

----

# 1.5.0 (2025-09-08)

## Features

- **Support storing DOS attributes on FreeBSD, Linux, and Windows**

    Previously, DOS attributes (`ARCHIVE`, `HIDDEN`, `READONLY`, `SYSTEM`) were
    only supported on Linux and only when using the FAT file system. This commit
    extends support to FreeBSD and Windows, and adds the ability to use
    additional file systems.

    DOS attributes can now be stored using either native file system support
    or extended attributes (`"user.NetMountAttrs"`). This allows DOS attributes
    to be stored even on file systems that do not natively support them,
    as long as they support extended attributes.

    Support for storing attributes on directories has also been added;
    previously, only files were supported.

    A new shared directory option was introduced:

    `attrs=<storage_method>`  Method to store DOS attributes:
                              `AUTO`, `IGNORE`, `NATIVE`, `EXTENDED` (default: `AUTO`)

    - `AUTO`     - Automatically select the best available method.

    - `IGNORE`   - Ignore all DOS attribute requests. Reading returns
                 `ARCHIVE` for files, none for directories.

    - `NATIVE`   - Use OS-native support (typically via file system).

    - `EXTENDED` - Store one-byte DOS attributes in the extended attribute
                 `"user.NetMountAttrs"`.

    Method support by OS:

    - All platforms support `IGNORE`.

    - FreeBSD and Linux support `NATIVE` (FAT only) and `EXTENDED`.
      `AUTO` mode on these systems prefers:

        1. `NATIVE` (if supported)
        2. `EXTENDED` (if supported)
        3. `IGNORE` (as fallback)

    - Windows supports `NATIVE` (via standard API) and defaults to it in `AUTO` mode.

- **Check DOS `READONLY` attribute when opening for write, writing, or deleting**

    When running on Windows, the server uses native support for DOS
    attributes, and the operating system itself enforces `READONLY` checks
    before write or delete operations. However, on other operating systems
    - especially when DOS attributes are stored as extended attributes -
    the OS ignores them, treating them as regular user data.

    The server now explicitly checks the DOS `READONLY` attribute before
    allowing operations that would modify a file, such as opening for writing,
    writing to the file, deleting a file, or removing a directory. This ensures
    access control when sharing files with DOS-style attributes.

- **Support Unicode filenames on Windows**

    Thanks to support for mapping long and non-ASCII filenames to
    the DOS 8.3 format, it is possible to share files with non-ASCII names.
    The issue occurred when running on Windows, where the `fopen`, `rename`,
    `stat`, and `truncate` functions use the local 8-bit encoding for
    filenames, which depends on the system locale.

    The `rename` and `stat` functions have been replaced with C++
    `std::filesystem` equivalents, which work correctly with UTF-16
    on Windows. The `fopen` function has been replaced with
    the Windows-specific `_wfopen`, which supports UTF-16.
    The `truncate` function has been reimplemented specifically for Windows.

## Other

- **ReplyCache – check whole request, cache no responses**

    The response uses the same sequence number as the request. Originally,
    the server only checked whether the cached response had the same
    sequence number as the newly received request. If so, the request
    was not processed further and the cached response was reused.

    Now, the request itself is also stored in the cache, not just
    the response. When a new request arrives with the same sequence number,
    its length is compared first, and if it matches, the entire content
    of the request is compared as well.

    This prevents a situation where a response to a previous request could
    be incorrectly reused for a different request with the same sequence
    number. While such collisions are rare, they can occur - for example,
    if a client's last request used sequence number 1 and the client
    then restarted, it might send a different request with the same sequence
    number (1) after restart.

    Another change concerns requests that do not trigger a response
    (malformed requests). Previously, repeated invalid requests would be
    reprocessed each time. Now, such requests are also cached along
    with the information that no response was sent, preventing repeated
    processing.

----

# 1.4.2 (2025-07-20)

## Optimizations

- **Optimize volume label handling by skipping directory list**

    Volume label requests are now handled directly, without generating
    the directory list.

## Compatibility with older systems

- **Use `chrono::system_clock` instead of `chrono::utc_clock` in log**

    `std::chrono::system_clock` is sufficient for logging purposes.
    The resulting binary is smaller, and the `system_clock` is supported
    by older standard libraries, which improves compatibility. For example,
    the standard library in FreeBSD 14.2 does not support
    `std::chrono::utc_clock`.

- **udp_socket_win: Use our to_big/from_big instead of hton/ntoh**

    Replaced calls to `htons`, `ntohs`, and `ntohl` with our own `to_big16`,
    `from_big16`, and `from_big32` functions. These are already used elsewhere
    in the codebase, so we no longer need to rely on the Windows versions.

- **udp_socket_win: Dynamic load "ws2_32.dll" at runtime when needed**

    The "ws2_32.dll" dependency is now loaded dynamically at runtime, and only
    if networking functionality is actually required. Previously, the library
    was linked at build time, which meant network functions were always needed,
    even when unused - such as when using serial port sharing.

    When serial port sharing is used, the server relies on a built-in,
    OS-independent network implementation. Therefore, there's no need to
    depend on Windows networking libraries at all.

    This change allows the server to run and share data over a serial port
    even on Windows systems without networking support (e.g., stripped-down
    configurations). Previously, Windows Vista or later was required due to
    link-time dependencies on `inet_pton` and `inet_ntop`.

- **udp_socket_win: inet_pton/ntop fallback for pre-Vista Windows**

    On Windows, `inet_pton` and `inet_ntop` are only supported starting with
    Windows Vista. To maintain compatibility with older versions, custom
    helper functions `ip_from_string` and `ip_to_string` were introduced.

    These helpers use `inet_pton`/`inet_ntop` when available, and fall back
    to the legacy `inet_addr` and `inet_ntoa` functions otherwise. This
    ensures IPv4 support across a wider range of Windows versions.
    The legacy functions have been available since the early days of IPv4
    support in Windows - at least since Windows 95 and Windows NT 3.1.

## Other

- **udp_socket_win: Log NOTICE if falling back to inet_ntoa/addr**

    Logs a NOTICE if `inet_pton` and `inet_ntop` are unavailable (likely
    on pre-Vista Windows), and the server falls back to using `inet_addr`
    and `inet_ntoa`.

----

# 1.4.1 (2025-07-12)

## Fixes

- **Fix --help message - remove <> from label and name_conversion**

- **Fix SLIP receive data length check**

    Fixed an off-by-one error in the SLIP frame decoding logic.
    The server incorrectly reported a buffer overflow when receiving data
    exactly equal to the buffer size (1500 bytes), effectively limiting
    the maximum receive size to 1499 bytes (MTU 1499).
    Sending was unaffected and allowed full 1500-byte frames.
    This issue has now been resolved.

----

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
