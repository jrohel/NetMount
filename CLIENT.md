# `netmount` (DOS Client)

- A TSR driver for DOS that allows mounting shared directories from one or more remote machines as local drives.
- It should work with MS-DOS 5.0 and newer and with sufficiently compatible systems such as FreeDOS.
- Has minimal dependencies — only a DOS Packet Driver class 1 (Ethernet II) is required.
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
/?                        Display this help
```

## Using the Netmount DOS Client
The netmount DOS client supports any interface for which a DOS Packet Driver class 1 (Ethernet II) exists,
including Ethernet network adapters, serial, parallel, and other hardware interfaces.
To use it, we must first install a Packet Driver in DOS. Then we install and configure the netmount client.
After that, we can mount and unmount remote directories/disks.

It is also recommended to set **LASTDRIVE** in "CONFIG.SYS". For example, `LASTDRIVE=Z` allows us to connect
drives up to `Z`. That means DOS can access a total of 26 drives. Netmount allows the use of drive letters
starting from `C`, which means a maximum of 24 drives can be used. On the other hand, MS-DOS allocates
a data structure in memory (RAM) for each drive specified by the LASTDRIVE parameter, so specifying more
drives than necessary wastes memory.


## Examples

1. **Install Packet driver (for Realtek RTL8139 in this example)**

    `rtspkt.com -p 0x60`

    - **-p**: Disables promiscuous mode (NetMount does not require it)
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

    `netmount install /IP:192.168.100.10 /MTU:576 /NO_ARP_REQUESTS`

    - **/IP:192.168.100.10**: Local (client) IP address
    - **/MTU:576**: Reduce MTU to 576 bytes
    - **/NO_ARP_REQUESTS**: Don't send ARP requests. SLIP operates at the IP layer and does not use MAC addresses.

3. **Mount shares**

    `netmount mount 192.168.100.2/C G`

The example uses an MTU of 576 bytes. On slow links, a small MTU is often used to prevent a single packet
transfer from blocking the line for too long. A common value is 576 bytes, which is the minimum MTU size
for the IPv4 protocol as defined in RFC 791. A smaller MTU means data must be split into more, smaller
fragments, increasing protocol overhead and reducing transmission speed.

In our case, if only NetMount is communicating over the serial link, it's more efficient to use larger frames.
The only limitation is the maximum packet size supported by the Packet Driver and the remote server.


## Memory Usage

NetMount does not perform any dynamic memory allocation. All variables, buffers, and stacks
are statically defined and embedded directly in the executable image. The codebase is
logically split into two parts:

- The first part, the TSR (Terminate and Stay Resident) component, remains in memory
  after installation.

- The second part includes routines for installation, uninstallation, mounting and unmounting
  drives, and displaying help. This portion is released after execution.

Only the TSR component stays resident, occupying a single contiguous memory block.

