# NetMount
-----

**NetMount** enables DOS systems to access shared directories from remote machines as if they were local drives. It operates over the UDP protocol. The DOS client supports any interface for which a DOS Packet Driver class 1 (Ethernet II) exists, including Ethernet network adapters, serial, parallel, and other hardware interfaces. NetMount is optimized for low-resource environments, making it suitable for retro computing, embedded platforms, and legacy system integration.

It consists of two core components:

- **`netmount`**: A lightweight TSR (Terminate and Stay Resident) driver for DOS that allows access to remote directories as standard network drives.
- **`netmount-server`**: A cross-platform server application for POSIX-compliant operating systems (Linux, *BSD, macOS, etc.) and Microsoft Windows, designed to share directories with DOS clients over a network or serial port using built-in SLIP protocol support.

In addition to these core components, the NetMount project includes supporting utilities:

- **`ninstall`**: A configuration-based installer (loader) for the DOS netmount client. [More info about the `ninstall`](NINSTALL.md)
- **`nmmanage`**: A command-line management tool for inspecting and modifying the configuration of an installed NetMount DOS client. [More info about the `nmmanage`](NMMANAGE.md)

-----
## `netmount` (DOS Client)

- A TSR driver for DOS that allows mounting shared directories from one or more remote machines as local drives.
- It should work with MS-DOS 5.0 and newer and with sufficiently compatible systems such as FreeDOS.
- Has minimal dependencies - only a DOS Packet Driver class 1 (Ethernet II) is required.
- Implements Ethernet Type II frame, ARP, IPv4, UDP and its own NetMount protocol.
- Supports any network interface for which a DOS Packet Driver class 1 (Ethernet II) is available. This includes Ethernet adapters, as well as serial, parallel, and other interfaces supported through appropriate drivers.
- Does not require a system reboot when mounting additional or unmounting drives.
- Written in C99 and assembler for the Open Watcom v2 compiler.

### Usage:
```
NETMOUNT INSTALL /IP:<local_ipv4_addr> [/MASK:<net_mask>] [/GW:<gateway_addr>]
         [/PORT:<local_udp_port>] [/PKT_INT:<packet_driver_int>]
         [/MTU:<size>] [/NO_ARP_REQUESTS]

NETMOUNT MOUNT [/CHECKSUMS:<names>] [/MIN_RCV_TMO:<seconds>]
         [/MAX_RCV_TMO:<seconds>] [/MAX_RETRIES:<count>]
         [/MIN_READ_LEN:<length>]
         <remote_ipv4_addr>[:<remote_udp_port>]/<remote_drive_letter>
         <local_drive_letter>

NETMOUNT UMOUNT <local_drive_letter>

NETMOUNT UMOUNT /ALL

NETMOUNT UNINSTALL

Commands:
INSTALL                   Installs NetMount as resident (TSR)
MOUNT                     Mounts remote drive as local drive
UMOUNT                    Unmounts local drive(s) from remote drive
UNINSTALL                 Uninstall NetMount

Arguments:
/IP:<local_ipv4_addr>     Sets local IP address
/PORT:<local_udp_port>    Sets local UDP port. 12200 by default
/PKT_INT:<packet_drv_int> Sets interrupt of used packet driver.
                          First found in range 0x60 - 0x80 by default.
/MASK:<net_mask>          Sets network mask
/GW:<gateway_addr>        Sets gateway address
/MTU:<size>               Interface MTU (560-1500, default 1500)
/NO_ARP_REQUESTS          Don't send ARP requests. Replying is allowed
<local_drive_letter>      Specifies local drive to mount/unmount (e.g. H)
<remote_drive_letter>     Specifies remote drive to mount/unmount (e.g. H)
/ALL                      Unmount all drives
<remote_ipv4_addr>        Specifies IP address of remote server
<remote_udp_port>         Specifies remote UDP port. 12200 by default
/CHECKSUMS:<names>        Enabled checksums (IP_HEADER,NETMOUNT; both default)
/MIN_RCV_TMO:<seconds>    Minimum response timeout (1-56, default 1)
/MAX_RCV_TMO:<seconds>    Maximum response timeout (1-56, default 5)
/MAX_RETRIES:<count>      Maximum number of request retries (0-254, default 4)
/MIN_READ_LEN:<length>    Minimum data read len (0-64, power of 2, default 64)
/?                        Display this help
```

[More info about the client](CLIENT.md)

-----
## `netmount-server` (Directory Sharing Server)

- A cross-platform user-space application that shares directories over the network.
- Any directory can be shared, including the root (/) directory.
- The physical location of the directory is irrelevant (hard disk, RAM-based, CD-ROM, network-mounted drive, ...)
- Uses the UDP protocol.
- Includes built-in implementations of the IP, UDP, and SLIP protocols.
- Implements file name conversion to the DOS 8.3 format, including Unicode-to-ASCII transliteration.
- Supports POSIX-compliant operating systems (Linux, *BSD, macOS, etc.) and Microsoft Windows.
- Can run as a non-root/unprivileged user.
- Supports running multiple instances concurrently, each using a unique IP/port combination or
  a dedicated serial device.
- CPU architecture independent. Although it is currently developed on x86-64, the application is designed
  to be portable and should work on other architectures. It supports both little-endian and big-endian systems.
- Written in C++20. Compilation tested with GCC and Clang.


### Usage:
```
./netmount-server [--help] [--bind-addr=<IP_ADDR>] [--bind-port=<UDP_PORT]
[--slip-dev=<SERIAL_DEVICE> --slip-speed=<BAUD_RATE>] [--slip-rts-cts=<ENABLED>]
[--translit-map-path=<PATH>] [--log-level=<LEVEL>]
<drive>=<root_path>[,attrs=<storage_method>][,label=<volume_label>][,name_conversion=<method>]
[... <drive>=<root_path>[,label=<volume_label>][,name_conversion=<method>]]

Options:
  --help                      Display this help
  --bind-addr=<IP_ADDR>       IP address to bind to (default: "0.0.0.0" - all addresses). Not supported in SLIP mode
  --bind-port=<UDP_PORT>      UDP port to listen on (default: 12200)
  --slip-dev=<SERIAL_DEVICE>  Serial device used for SLIP (host network is used by default)
  --slip-speed=<BAUD_RATE>    Baud rate of the SLIP serial device
  --slip-rts-cts=<ENABLED>    Enable hardware flow control: 0 = OFF, 1 = ON (default: OFF)
  --translit-map-path=<PATH>  Unicode-to-ASCII map file (default: "netmount-u2a.map"; empty disables)
  --log-level=<LEVEL>         Logging verbosity level: 0 = OFF, 7 = TRACE (default: 3)
  <drive>=<root_path>         drive - DOS drive C-Z, root_path - path to serve
  attrs=<storage_method>      File attribute storage method: AUTO, IGNORE, NATIVE, EXTENDED (default: AUTO)
  label=<volume_label>        volume label (first 11 chars used, default: NETMOUNT; use "--label=" to remove)
  name_conversion=<method>    file name conversion method: OFF, RAM (default: RAM)
```

[More info about the server](SERVER.md)

-----
## Motivation
This project started from a need to access shared directories from remote machines on old laboratory device running DOS. This device lacked a network card and only had an RS232 serial port and a parallel port. I needed a solution that:

- Works over IP (including SLIP via serial port),
- Has minimal memory and CPU requirements.
- Is portable.

After researching available tools, I found nothing that fully met these needs. So I decided to build my own.

In the past, I have developed software for a number of industrial embedded devices, such as protocol converters, drivers for specialized hardware, and real-time signal processing systems. Therefore, creating new software is not a problem for me. However, the problem often lies in the availability of documentation. When developing NetMount, I could not find an official standard (API documentation) for the MS-DOS driver interface. As a result, I relied on unofficial documentation and existing source code that I found on the Internet.

I would like to mention https://mirror.cs.msu.ru/oldlinux.org/Linux.old/docs/interrupts/int-html/,
RBIL (Ralf Brown’s Interrupt List) and the source code of etherdfs-server and etherdfs-client. However, my thanks go to the authors of all the useful resources that I found on the Internet. Thank you.

----
## Development & Testing
### DOS Client:
- **Build:** Compiled using Open Watcom v2.
- **Emulated environment testing:** Tested with DOSEMU on Linux.
- **Real hardware testing:** Tested on MS-DOS 6.22 and FreeDOS 1.3.
- **Serial communication testing:** Using the ethersl.com SLIP packet driver from Crynwr.

### Server:
- **POSIX version:** Compiled on Linux and FreeBSD. Tested on Linux.
- **Windows version:** Cross-compiled on Linux using MinGW. Tested with Wine on Linux.

----
## License
I decided to publish my work as open source to help other people. This project is licensed under the GPLv2 License. You can freely use, modify, and distribute it, as long as you comply with the terms of the GPLv2 license.