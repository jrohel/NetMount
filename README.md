# NetMount
-----------
**NetMount** enables DOS systems to access shared directories from remote machines as if they were local drives. It operates over the UDP protocol. The DOS client supports any interface for which a DOS Packet Driver class 1 (Ethernet II) exists, including Ethernet network adapters, serial, parallel, and other hardware interfaces. NetMount is optimized for low-resource environments, making it suitable for retro computing, embedded platforms, and legacy system integration.

It consist of two components:

- **`netmount`**: A lightweight TSR (Terminate and Stay Resident) driver for DOS that allows access to remote directories as standard network drives.
- **`netmount-server`**: A cross-platform server application for POSIX-compliant operating systems (Linux, *BSD, macOS, etc.) and Microsoft Windows, designed to share directories with DOS clients over the network.

-----
## `netmount` (DOS Client)
- A TSR driver for DOS that allows mounting shared directories from one or more remote machines as local drives.
- It should work with MS-DOS 5.0 and newer and with sufficiently compatible systems such as FreeDOS.
- Has minimal dependencies — only a DOS Packet Driver class 1 (Ethernet II) is required.
- Implements Ethernet Type II frame, ARP, IPv4, UDP and its own NetMount protocol.
- Supports any network interface for which a DOS Packet Driver class 1 (Ethernet II) is available. This includes Ethernet adapters, as well as serial, parallel, and other interfaces supported through appropriate drivers.
- Does not require a system reboot when mounting additional or unmounting drives.
- Written in C99 and assembler for the Open Watcom v2 compiler.

### Usage:
```
NETMOUNT INSTALL /IP:<local_ipv4_addr> [/MASK:<net_mask>] [/GW:<gateway_addr>]
         [/PORT:<local_udp_port>] [/PKT_INT:<packet_driver_int>]
         [/NO_ARP_REQUESTS]

NETMOUNT MOUNT [/MIN_RCV_TMO:<seconds>] [/MAX_RCV_TMO:<seconds>]
         [/MAX_RETRIES:<count>]
         <remote_ipv4_addr>[:<remote_udp_port>]/<remote_drive_letter>
         <local_drive_letter>

NETMOUNT UMOUNT <local_drive_letter>

NETMOUNT UMOUNT /ALL

Commands:
INSTALL                   Installs NetMount as resident (TSR)
MOUNT                     Mounts remote drive as local drive
UMOUNT                    Unmounts local drive(s) from remote drive

Arguments:
/IP:<local_ipv4_addr>     Sets local IP address
/PORT:<local_udp_port>    Sets local UDP port. 12200 by default
/PKT_INT:<packet_drv_int> Sets interrupt of used packet driver.
                          First found in range 0x60 - 0x80 by default.
/MASK:<net_mask>          Sets network mask
/GW:<gateway_addr>        Sets gateway address
/NO_ARP_REQUESTS          Don't send ARP requests. Replying is allowed
<local_drive_letter>      Specifies local drive to mount/unmount (e.g. H)
<remote_drive_letter>     Specifies remote drive to mount/unmount (e.g. H)
/ALL                      Unmount all drives
<remote_ipv4_addr>        Specifies IP address of remote server
<remote_udp_port>         Specifies remote UDP port. 12200 by default
/MIN_RCV_TMO:<seconds>    Minimum response timeout (1-56, default 1)
/MAX_RCV_TMO:<seconds>    Maximum response timeout (1-56, default 5)
/MAX_RETRIES:<count>      Maximum number of request retries (0-254, default 4)
/?                        Display this help
```

-----
## `netmount-server` (Directory Sharing Server)
- A cross-platform user-space application that shares directories over the network.
- Supports POSIX-compliant operating systems (Linux, *BSD, macOS, etc.) and Microsoft Windows.
- Can run as a non-root/unprivileged user.
- Supports multiple simultaneous instances, each with unique IP/port bindings.
- CPU architecture independent. Although it is currently developed on x86-64, the application is designed to be portable and should work on other architectures. It supports both little-endian and big-endian systems.
- Written in C++20. Tested with GCC and Clang.


### Usage:
```
Usage:
./netmount-server [--help] [--bind_ip_addr=] [--bind_port=udp_port] <drive>=<root_path> [... <drive>=<root_path>]

Options:
  --help                   Display this help
  --bind-addr=<IP_ADDR>    IP address to bind, all address ("0.0.0.0") by default
  --bind-port=<UDP_PORT>   UDP port to listen, 12200 by default
  <drive>=<root_path>      drive - DOS drive C-Z, root_path - paths to serve
```

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