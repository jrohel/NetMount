# `NMManage` (NetMount DOS Client Manager)

**NMManage** is a command-line utility for reading and modifying the parameters of an installed NetMount DOS client.
It provides information about the NetMount installation, network settings, mounted drives, and IP-to-MAC address mappings, while also allowing the user to configure network and drive-specific options.

## Key Features

- Display general information about NMManage and the installed NetMount client.
- Inspect and configure global network settings.
- List all currently mounted network drives and view or modify individual drive options.
- Access and modify IP-to-MAC address table entries.
- Written in C99 and assembler for the Open Watcom v2 compiler.

## Usage
```
NMMANAGE INFO
NMMANAGE NET INFO
NMMANAGE NET GET <get_net_option>
NMMANAGE NET SET <set_net_option> <value>
NMMANAGE DRIVES
NMMANAGE DRIVE <local_drive_letter> INFO
NMMANAGE DRIVE <local_drive_letter> GET <get_drive_option>
NMMANAGE DRIVE <local_drive_letter> SET <set_drive_option> <value>
NMMANAGE MACS
NMMANAGE MAC GET <ipv4_addr>
NMMANAGE MAC SET <ipv4_addr> <mac_addr>

Commands:
INFO                  Show info about NMManager and detected installed NetMount
NET INFO              Show current NetMount network settings
NET GET               Get value of a network option
NET SET               Set value of a network option
DRIVES                List all mounted network drives
DRIVE INFO            Show details (all options) for a specific mounted drive
DRIVE GET             Get value of a drive mount option
DRIVE SET             Set value of a drive mount option
MACS                  List IP-to-MAC address table entries
MAC GET               Get MAC address for IP
MAC SET               Set MAC address for IP
/?                    Display this help

Arguments:
<local_drive_letter>  Specifies the mounted drive to work with (e.g. H)
<get_net_option>      IP, MASK, GW, PORT, MTU, ARP_REQUESTS, PKT_INT, MAC
<set_net_option>      PORT, MTU, ARP_REQUESTS
<get_drive_option>    IP, PORT, DRIVE, MIN_READ_LEN, MIN_RCV_TMO, MAX_RCV_TMO,
                      MAX_RETRIES, CHECKSUM_NETMOUNT, CHECKSUM_IP_HEADER
<set_drive_option>    MIN_READ_LEN, MIN_RCV_TMO, MAX_RCV_TMO,
                      MAX_RETRIES, CHECKSUM_NETMOUNT, CHECKSUM_IP_HEADER
<value>               Specifies value to set
<ipv4_addr>           Specifies the IPv4 address to work with
<mac_addr>            Specifies the MAC address to set
```

## Examples

**Show info about NMManager and detected installed NetMount**
```
C:\>nmmanage info
NMManage
Version: 1.0.0
ABI version: 1

Detected installed NetMount client
Version: 1.7.0
ABI version: 1
```

**Show current NetMount network settings**
```
C:\>nmmanage net info
IP: 192.168.122.10
MASK: 255.255.255.0
GW: 192.168.122.1
Local udp PORT: 12200
Interface MTU: 1500
Send ARP_REQUESTS: ENABLED
PKT_INT: 0x60
MAC: 64:62:E9:90:78:78
```

**Get local IP address**
```
C:\>nmmanage net get ip
192.168.122.10
```

**Set interface MTU**
```
C:\>nmmanage net set mtu 1450
```

**List all mounted network drives**
```
C:\>nmmanage drives
H -> 192.168.122.1:12200/C
I -> 192.168.122.1:12200/D
J -> 192.168.122.2:12205/C
```

**Show details (all options) for a specific mounted drive**
```
C:\>nmmanage drive h info
Local drive: H
Server IP: 192.168.122.1
Server udp PORT: 12200
Server DRIVE: C
Minimum length of data block read from the server MIN_READ_LEN [bytes]: 64
Minimum response timenout MIN_RCV_TMO [seconds]: 1
Maximum response timenout MAX_RCV_TMO [seconds]: 5
Maximum number of request retries MAX_RETRIES: 4
Netmount protocol checksum CHECKSUM_NETMOUNT: ENABLED
IP header checksum CHECKSUM_IP_HEADER: ENABLED
```

**Get server IP address for drive H**
```
C:\>nmmanage drive h get ip
192.168.122.1
```

**Set maximum response timenout MAX_RCV_TMO for drive H to 4 seconds**
```
C:\>nmmanage drive h set max_rcv_tmo 4
```

**List IP-to-MAC address table entries**
```
C:\>nmmanage macs
192.168.122.002   38:7c:76:01:12:45
192.168.122.001   52:54:00:6E:19:31
```

**Get MAC address for IP 192.168.122.1**
```
C:\>nmmanage mac get 192.168.122.1
52:54:00:6E:19:31
```

**Set MAC address for IP 192.168.122.1 to the broadcast address FF:FF:FF:FF:FF:FF**
```
C:\>nmmanage mac set 192.168.122.1 ff:ff:ff:ff:ff:ff
```
If the MAC address `FF:FF:FF:FF:FF:FF` is set for an IP address, the NetMount client will first attempt
to determine the peer’s actual MAC address by sending an ARP request before contacting that IP address
again - unless ARP requests are disabled using the NetMount client INSTALL option `/NO_ARP_REQUESTS` or
via `nmmanage net set arp_requests disabled`.

Even if sending ARP requests is disabled, or if a different MAC address (other than `FF:FF:FF:FF:FF:FF`)
is already set, the client can still learn or update the peer’s MAC address from an ARP request
sent by the peer when it queries the MAC address of the NetMount client.
