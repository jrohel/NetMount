# `netmount-server` (Directory Sharing Server)

- A cross-platform user-space application that shares directories over the network.
- Any directory can be shared, including the root (/) directory.
- The physical location of the directory is irrelevant (hard disk, RAM-based, CD-ROM, network-mounted drive, ...)
- Uses the UDP protokol
- Implements file name conversion to DOS 8.3 format.
- Supports POSIX-compliant operating systems (Linux, *BSD, macOS, etc.) and Microsoft Windows.
- Can run as a non-root/unprivileged user.
- Supports multiple simultaneous instances, each with unique IP/port bindings.
- CPU architecture independent. Although it is currently developed on x86-64, the application is designed
  to be portable and should work on other architectures. It supports both little-endian and big-endian systems.
- Written in C++20. Tested with GCC and Clang.


## Usage
```
./netmount-server [--help] [--bind_ip_addr=] [--bind_port=udp_port] <drive>=<root_path>[,name_conversion=<method>] [... <drive>=<root_path>[,name_conversion=<method>]]

Options:
  --help                      Display this help
  --bind-addr=<IP_ADDR>       IP address to bind, all address ("0.0.0.0") by default
  --bind-port=<UDP_PORT>      UDP port to listen, 12200 by default
  <drive>=<root_path>         drive - DOS drive C-Z, root_path - paths to serve
  <name_conversion>=<method>  file name conversion method - OFF, RAM (RAM by default)
```


## Examples of use on Linux

**Simple sharing of two directories**

`netmount-server C=/srv/dos_programs D=/srv/data`

- Listens (binds) on all network interfaces using the default port `12200`
- Filename conversion is enabled for both shared directories

**Advanced sharing of two directories**

`netmount-server --bind-addr=192.168.200.1 C=/srv/dos_programs,name_conversion=OFF D=/srv/data`

- Listens only on IP address `192.168.200.1` using the default port `12200`
- Filename conversion is enabled only for `D` and disabled for `C` (e.g., "/srv/dos_programs" uses
  a DOS-compatible filesystem with short filenames and is case-insensitive)

**Sharing the Current Working Directory as Drive D**

`netmount-server D=.`

**Sharing a Directory Mounted via SFTP from Another Server**
```
sshfs server_addr:/home/remote_user/data /home/user/net/  # Mount remote disk via SFTP
netmount-server D=/home/user/net                          # Share the mounted content via netmount-server
```


## Examples of use on Windows

**Sharing a Directory as Drive C and Drive E as Drive D**

`netmount-server.exe C=C:\INSTALL D=E:`

**Sharing the Current Working Directory**

`netmount-server.exe D=.`

## Server file names conversion to DOS 8.3 format

The server implements its own conversion of existing file names to DOS short names 8.3. The advantage
of the conversion is that it is independent of the operating system and file system. The disadvantage
is that the mapping of converted file names to existing ones is only temporary in RAM and is unstable
(re-created during some operations).

The problem occurs when we have multiple long file names with the same beginning and then we delete
some of them.

Example:

    Initial mapping on the server:
        LONG_N~1.TXT -> long_name1.txt
        LONG_N~2.TXT -> long_name2.txt
    Commands on the client:
        DEL LONG_N~1.TXT
        DIR
    New mapping on the server:
        LONG_N~1.TXT -> long_name2.txt

So, the file `LONG_N~1.TXT` (on the server `long_name1.txt`) has been deleted. But after the `DIR` command
(re-creating the mapping), `LONG_N~2.TXT` disappears and `LONG_N~1.TXT` reappears, now pointing to
`long_name2.txt`.

### Conversion description

- Numbers '0' - '9', uppercase ASCII letters 'A' - 'Z' and characters '!', '#', '$', '%', '&', ''', '(',
  ')', '-', '@', '^', '_', '`', '{', '}', '~' are preserved.

- Lowercase ASCII letters 'a' - 'z' are converted to uppercase ASCII letters 'A' - 'Z'.

- Space characters ' ' are preserved except for the trailing ones (in DOS they are considered to be padding
  and not a part of the filename).

- Other characters are omitted.

- If any character is omitted based on the above conversion, or the name was too long, the name is modified
  to end with `~<number>`.

- If a name collision occurs after lower to upper case conversion, the new name is modified to end with
  `~<number>`.

- `<number>` is a number in the range 1-9999. It starts with 1 for a specific prefix and increases by 1.

### Argument `name_conversion=<method>`
The server accepts optional argument `name_conversion=<method>` in the shared drive definition.
Supported `<method>` are `RAM` and `OFF`. The default is `RAM`. `OFF` turns off file name conversion.

`OFF` is preferred if we are sure that:

- the files on the server comply with DOS 8.3 rules and

- the file system is case insensitive or the file names are all lowercase.

The argument is separated by a comma. If there is a comma in the path to the shared directory, use
the '\' character as an escape. Do not share the same directory with different `name_conversion` using
the same server process!

Example usage:

`netmount-server C=/share/c,name_conversion=OFF D=/data 'G=/share_with\,comma'`


## Known limitations
The shared directory can be on any filesystem. However, if a filesystem other than "msdos" is used,
various filename restrictions must be taken into account, as NetMount supports DOS Short Names.

- **File name length is limited to 8 + 3 characters:** 8 characters for the name and 3 characters
  for the extension. Longer names are either converted or truncated if name conversion is turned off
  (argument `name_conversion=OFF`). If conversion is disabled, files with truncated names cannot be accessed.

- **DOS assumes case-insensitive file names:** This usually applies only to ASCII characters, as case
  conversion of national characters depends on the current codepage. Unix-based systems typically have
  case-sensitive file names. Therefore, netmount-server performs case conversion on file names.
  On the server, file names are created in lowercase. Existing files with uppercase ASCII characters
  are converted. National characters are omitted because they cannot be converted without knowledge
  of the DOS client's codepage. When name conversion is turned off (argument `name_conversion=OFF`),
  files with uppercase characters in their names cannot be accessed. Disable this only if you are using
  a case-insensitive filesystem or are certain the files will only have lowercase names.

- **National characters:** Modern filesystems support Unicode. DOS Short Names encode characters in 8 bits,
  which covers only a small subset of Unicode. Additionally, the mapping of codes above 128 to characters
  depends on the client's codepage. Currently, netmount-server does not support character encoding
  conversion, but as mentioned earlier, full conversion is not possible. Instead, national characters
  are omitted during conversion. When conversion is disabled (argument `name_conversion=OFF`), characters
  are passed unchanged, which is typically not suitable, as the client’s codepage may differ
  from the encoding used on the server. It is ideal to use only ASCII characters in file names.

- **DOS further disallows certain control and special characters in file names:** ('', '/', ':', '*',
  '?', '"', '<', '>', '|'). These are omitted during conversion. When conversion is disabled (argument
  `name_conversion=OFF`), these characters are passed unchanged, which may cause issues.

- **Another limitation** is that netmount-server supports FAT attributes (ARCHIVE, HIDDEN, READ-ONLY,
  SYSTEM) only on Linux, and only when using the "msdos" filesystem. Support for other operating systems
  and filesystems may be added in the future (for example, by mapping to extended attributes).


## Security
When sharing private data over an untrusted network (e.g., the Internet), it is strongly recommended to use
additional security measures, such as a VPN. Unsecured sharing should only be used for public, read-only data.

- The protocol does not protect data against eavesdropping — the data is not encrypted.

- It does not provide client authentication; anyone can mount the disk.

- It does not protect against man-in-the-middle attacks. While data has a checksum by default,
  it is weak and only serves to detect transmission errors. An attacker can modify the data during
  transmission and recalculate the checksum.


## Sharing over a serial port
The netmount-server uses the UDP protocol, so sharing works over any medium that supports IP and UDP
transmission. For transmitting the IP protocol over a serial port, the simple SLIP (Serial Line
Internet Protocol) protocol can be used.

### SLIP Configuration Example on Linux

1. **Connecting the Serial Port using `slattach`**

    First, connect the serial port for the SLIP connection using the `slattach` command. In this example,
    the serial port `/dev/ttyS0` is used (you may need to adjust this to match your configuration):

    `slattach -p slip -s 115200 /dev/ttyS0`

    - **-p slip**: Specifies that the SLIP protocol is used.

    - **-s 115200**: Sets the baud rate to 115200 (adjust as needed).

    - **/dev/ttyS0**: The serial port (use the correct port name for your setup).

2. **Setting IP Addresses for the SLIP Interface**

    After connecting the serial port, set the IP addresses for the SLIP interface.
    In example the local address is 192.168.200.1 and the remote address is 192.168.200.10:

    `# ifconfig sl0 192.168.200.1 pointopoint 192.168.200.10 mtu 1500 up  # The older method using ifconfig`

    `ip addr add 192.168.200.1 peer 192.168.200.10 dev sl0`

    `ip link set sl0 mtu 1500 up`

    - **sl0**: This is the SLIP interface.

    - **192.168.200.1**: The local IP address for the SLIP interface.

    - **192.168.200.10**: The remote IP address for the SLIP connection.

    - **mtu 1500**: The Maximum Transmission Unit (MTU) is set to 1500, which is the typical value
      for Ethernet. This step is optional, but it is recommended for optimal performance.

        Note: On slow links, a smaller MTU is often used to prevent a single packet from blocking
        the link for too long. A common value is 576, which is the minimum MTU size for the IPv4
        protocol as defined in RFC 791. A smaller MTU results in the data being split into more smaller
        pieces, leading to higher protocol overhead and slower transmission. In our case, if only
        netmount is communicating over the serial link, it is more efficient to use larger frames.
        Of course, this is limited by the maximum frame size supported by the server and the remote
        party (DOS Packet Driver).