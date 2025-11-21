# `NetMount` (DOS Client)

- A TSR driver for DOS that allows mounting shared directories from one or more remote machines as local drives.
- It should work with MS-DOS 5.0 and newer and with sufficiently compatible systems such as FreeDOS.
- Has minimal dependencies - only a DOS Packet Driver class 1 (Ethernet II) is required.
- Implements Ethernet Type II frame, ARP, IPv4, UDP and its own NetMount protocol.
- Supports any network interface for which a DOS Packet Driver class 1 (Ethernet II) is available.
  This includes Ethernet adapters, as well as serial, parallel, and other interfaces supported through
  appropriate drivers.
- Does not require a system reboot when mounting additional or unmounting drives.
- Written in C99 and assembler for the Open Watcom v2 compiler.

## Usage
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

## Using the Netmount DOS Client

The netmount DOS client supports any interface for which a DOS Packet Driver class 1 (Ethernet II) exists,
including Ethernet network adapters, serial, parallel, and other hardware interfaces.
To use it, we must first install a Packet Driver in DOS. Then we install and configure the netmount client.
After that, we can mount and unmount remote directories/disks.

The NetMount client supports up to four remote IP addresses, allowing directories to be mounted
from up to four servers. If one IP address is used only as a gateway (not as a NetMount server),
up to three servers can be accessed simultaneously. This limit helps keep the NetMount client small
and memory-efficient. The limit applies only to servers with different IP addresses. Connections
to multiple NetMount servers on the same IP address but different UDP ports are not restricted.

It is also recommended to set **LASTDRIVE** in "CONFIG.SYS". For example, `LASTDRIVE=Z` allows us to connect
drives up to `Z`. That means DOS can access a total of 26 drives. Netmount allows the use of drive letters
starting from `C`, which means a maximum of 24 drives can be used. On the other hand, MS-DOS allocates
a data structure in memory (RAM) for each drive specified by the LASTDRIVE parameter, so specifying more
drives than necessary wastes memory.


## Examples

1. **Install Packet driver (for Realtek RTL8139 in this example)**

    `rtspkt.com -p 0x60`

    - **-p**: Disables promiscuous mode (NetMount does not use promiscuous mode, so it is safe to disable it)
    - **0x60**: Packet driver interrupt

2. **Install NetMount client**

    `netmount install /IP:192.168.100.10 /MASK:255.255.255.0 /GW:192.168.100.1`

    - **/IP:192.168.100.10**: Local (client) IP address
    - **/MASK:255.255.255.0**: Network mask
    - **/GW:192.168.100.1**: Gateway IP address
    - The packet driver interrupt is detected automatically, MTU is 1500

3. **Mount shares**

    `netmount mount 192.168.100.1/C D`

    - Mount share C from server 192.168.100.1 as drive D:

    `netmount mount 192.168.100.2/C G`

    - Mount share C from server 192.168.100.2 as drive G:

    `netmount mount 192.168.100.2/E H`

    - Mount share E from server 192.168.100.2 as drive H:

4. **Unmount share G**

    `netmount umount G`

5. **Unmount all remaining shares**

    `netmount umount /ALL`

6. **Uninstall NetMount**

    `netmount uninstall`

    NetMount can only be uninstalled when no drives are mounted and it is the last handler
    in the INT 2Fh interrupt chain. If either condition is not met, an error is reported.
    If another program has hooked INT 2Fh after NetMount, its handler must be removed first.


## MTU

The standard MTU for Ethernet networks is 1500 bytes, and NetMount uses the same value by default.
However, if any part of the network path to the server has a lower MTU (e.g. a limited DOS packet driver,
a VPN that reduces the MTU due to additional headers, the MTU of server interface) then the MTU
on the NetMount client must be adjusted accordingly. The NetMount client’s MTU must never exceed
the smallest MTU along the route, as it defines the maximum size of packets that can be transmitted
or requested.

**Example:**

Server 192.168.200.2 is located in a remote network accessed via VPN. Since the VPN being used has
an MTU of 1420, we need to reduce the MTU on the NetMount client accordingly. The MTU setting is global.
It applies to the entire network interface, not to individual mounts. It is set during the netmount
install phase. As a result, all mounts will use smaller frame sizes. While this isn't ideal for transfer
speed, it still ensures reliable functionality.

    `netmount install /IP:192.168.100.10 /MASK:255.255.255.0 /GW:192.168.100.1 /MTU:1420`
    `netmount mount 192.168.100.2/C G`
    `netmount mount 192.168.200.2/C H`


## Read-ahead buffering

Some applications read files in very small blocks (e.g., 4 bytes at a time). The application
would request 4 bytes, then another 4 bytes after receiving the first, and so on. Reading
a 10 KB file 4 bytes at a time results in 2,500 network requests, which is extremely inefficient.
In the extreme case - reading byte by byte it is 10,000 network requests.

Over Ethernet, each such request carries around 50 bytes of headers (MAC, IP, UDP, NetMount),
plus additional low-level preambles and checksums - all for just a few bytes of actual data.
On high-throughput networks, latency becomes the limiting factor. Even on a local network
with 1 ms latency, 2,500 requests would take about 2.5 seconds to complete - a transfer
rate of just 4 KB/s when reading in 4-byte blocks. When reading byte by byte, this drops to 1 KB/s.
Over Wi-Fi with poor signal or over the Internet (e.g., with 20 ms latency), transfer speeds
can fall to just a few hundred bytes per second.

To address this, the NetMount client uses read-ahead buffering. By default, it uses a 64-byte buffer
and, when reading files, requests at least 64 bytes from the server to fill this buffer - assuming
that subsequent reads will be sequential and can be served directly from it.
Although 64 bytes is relatively small, this change results in up to 64x faster reads when
the application reads 1 byte at a time, and 16x faster for 4-byte blocks. Read-ahead buffering also
reduces the load on the network and the NetMount server. Serving a single 64-byte read is much less
demanding than handling 16 separate 4-byte requests, or 64 one-byte requests.

The size of the read-ahead buffer is configurable using the `/MIN_READ_LEN:<length>` argument.
The `<length>` value may be one of the following: `0`, `1`, `2`, `4`, `8`, `16`, `32`, or `64`.
Setting it to `0` disables read-ahead buffering entirely.
If the `/MIN_READ_LEN:<length>` argument is not provided, the default value of `64` is used.
This setting is independent for each mounted drive. However, internally, the NetMount client uses
a single shared 64-byte read-ahead buffer for all mounted drives, and the setting determines
how many bytes from the beginning of the buffer are used by each drive.

Read-ahead buffering is applied only when the read request is smaller than the configured buffer
size. Larger reads bypass the buffer. A slowdown may occur if the application performs many small
reads from random offsets that fall outside the buffered data. This read-ahead buffering never
increases the number of requests - only the number of bytes in responses may be larger.

Since the server-side data is not locked, there is a risk that it may be modified by another
application or client at any time. To avoid serving invalid data from the buffer,
its contents are considered valid for at most 5 seconds. After this period, the buffer
is invalidated and the next read will fetch fresh data from the server.


## Sharing a Network Interface Between NetMount and Other Applications

For each network interface card (NIC), only one packet driver can be installed in the system.
This driver operates at the link layer (Layer 2) and provides access to the network card.

The packet driver allows registering receive handlers based on the frame type.
The NetMount client uses Ethernet types 0x0800 (IPv4 protocol) and 0x0806 (ARP protocol).

Typically, a packet driver supports only one handler per specific frame type - meaning that only one
application can receive packets of a given type. As a result, once the NetMount client registers handlers
for these frame types, no other application can register for IPv4 or ARP on the same packet driver.

To overcome this limitation, a Packet Driver Multiplexer can be used. This is a TSR (Terminate and Stay
Resident) program that registers itself with the physical packet driver and exposes multiple virtual packet
driver interfaces. The multiplexer receives network frames from the real driver and distributes them
to registered virtual drivers based on the frame type. From the system's perspective, one physical NIC
can then be accessed as several independent virtual interfaces. NetMount can use one interface while other
applications use the others.

### Example: Using the Packet Driver Multiplexer (PKTMUX v1.2b) with Two Virtual Interfaces

1. **Install the Packet Driver**

    Install the packet driver for your network card as usual.
    The following example is for a Realtek RTL8139 card - replace with the appropriate driver for your hardware:

    `rtspkt.com -p 0x60`

    - **-p**: Disables promiscuous mode (NetMount does not use promiscuous mode, so it is safe to disable it unless another application depends on it)
    - **0x60**: Packet driver interrupt

2. **Install PKTMUX**

    Load the packet multiplexer and specify the number of virtual interfaces (channels).
    Here we create **2 channels**:

    `pktmux 2`

    - By default, PKTMUX attaches to the packet driver at `INT 0x60`.

3. **Install Virtual Packet Drivers**

    For each virtual interface (channel), load a virtual packet driver.
    Assign a unique software interrupt number to each:

    `pktdrv 63`

    `pktdrv 65`

    - This creates virtual drivers at `INT 0x63` and `INT 0x65` (hex).
    - Choose unused interrupt numbers (`0x60 - 0x7F`) to avoid conflicts.

4. **Install NetMount Client (on Second Virtual Interface)**

    Bind NetMount client to the virtual driver on interrupt `0x65` (decimal `101`).
    NetMount client versions up to and including 1.5.0 do not support hexadecimal values in /PKT_INT. Use the decimal value instead.

    `netmount install /PKT_INT:0x65 /IP:192.168.100.10 /MASK:255.255.255.0 /GW:192.168.100.1`

5. **Use the First Virtual Interface for Other Applications**

    Applications can be assigned a different IP address than the NetMount client. They can also use DHCP for their configuration,
    depending on the capabilities of the specific application.

Using a Packet Driver Multiplexer and running additional network applications may degrade data transfer
performance when accessing NetMount-mounted drives.


## Mounting a Shared Directories via Serial Port

The netmount uses the UDP protocol, so sharing works over any medium that supports IP and UDP transmission.
To transmit IP over a serial port, the simple SLIP (Serial Line Internet Protocol) protocol can be used.

### SLIP Configuration Example

1. **Install a serial port Packet Driver class 1 (Ethernet II)**

    `ethersl 0x60 3 0x2F8 115200`

    - **0x60**: Packet driver interrupt
    - **3**: Serial port hardware interrupt
    - **2F8**: Serial port I/O address
    - **115200**: Serial port baud rate

2. **Install the NetMount client**

    `netmount install /IP:192.168.100.10 /NO_ARP_REQUESTS`

    - **/IP:192.168.100.10**: Local (client) IP address
    - **/NO_ARP_REQUESTS**: Don't send ARP requests. SLIP operates at the IP layer and does not use MAC addresses.

3. **Mount shares**

    `netmount mount 192.168.100.2/C G`

The example uses the default MTU of 1500 bytes. On slow links, a smaller MTU is often chosen to prevent a single
packet transfer from occupying the line for too long. A commonly used value is 576 bytes (`/MTU:576`), which
is the minimum MTU defined for the IPv4 protocol in RFC 791. However, using a smaller MTU means that data
must be split into more, smaller fragments, which increases protocol overhead and reduces transmission efficiency.

The `ethersl.com` packet driver and the built-in SLIP implementation in the NetMount server support an MTU of 1500
bytes. The Linux SLIP driver supports an MTU of up to 65,534 bytes.

In our case, if only NetMount is communicating over the serial link, using the default MTU of 1500 bytes is
the most efficient option. However, if any part of the network path to the server has a lower MTU, the MTU setting
on the NetMount client must be adjusted accordingly.


## Memory Usage

NetMount does not perform any dynamic memory allocation. All variables, buffers, and stacks
are statically defined and embedded directly in the executable image. The codebase is
logically split into two parts:

- The first part, the TSR (Terminate and Stay Resident) component, remains in memory
  after installation.

- The second part includes routines for installation, uninstallation, mounting and unmounting
  drives, and displaying help. This portion is released after execution.

Only the TSR component stays resident, occupying a single contiguous memory block.

