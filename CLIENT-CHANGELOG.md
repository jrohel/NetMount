# 1.5.0 (2025-05-23)

## Features

- UNINSTALL command added - Uninstalls NetMount and deallocates its memory.

----

# 1.4.0 (2025-05-21)

## Features

- Configurable interface MTU

    Adds a new argument `/MTU:<size>` to the `install` command. `size` specifies the interface MTU
    (Maximum Transfer Unit). Supports MTU sizes in the range of 560 to 1500. The default value is 1500.

    Previously, the interface MTU was fixed at 1186 bytes (MAX_FRAMESIZE was 1200 bytes minus 14 bytes
    of Ethernet header).

## Other

- Updated help text: Added `/CHECKSUMS:<names>` argument to `mount` command. It was previously
  described in help but not listeded for the `mount`.

- Save used registers before calling packet driver

- pktdrv_recv: Use NetMount stack when calling other function

- Optimize assembly code - use `PUSHA`/`POPA` on 80186+ CPUs

----

# 1.3.0 (2025-04-24)

## Features

- Validate IP header checksum

- Configurable checksums:

    Adds a new argument `/CHECKSUMS:<names>` to the `mount` command. `<names>` is a comma-separated
    list of checksums to enable. Supported values ​​are `IP_HEADER` and `NETMOUNT`.
    By default, both are used.

    The client always sent and validated the IP header and NetMount protocol checksum.
    Now it is possible to define a list of checksums to use.

    **Examples:**

    `/CHECKSUMS:IP_HEADER` - uses only the checksum of the IP header

    `/CHECKSUMS:NETMOUNT`  - uses only the checksum of the NetMount protocol

    `/CHECKSUMS:`          - empty list, all checksums are disabled

    `/CHECKSUMS:IP_HEADER,NETMOUNT` - uses both checksums - default

    Note: The checksum of the IP header is always sent. It is mandatory. With the argument,
    we only disable the validation of the checksum of received IP headers.

----

# 1.2.0 (2025-04-09)

## Features

- Support for sending ARP requests.

    Previously, the client responded to ARP requests and learned the peer's HW (MAC) address from them.
    Until it learned the peer's HW address, it sent data as a broadcast - destination
    address `FF:FF:FF:FF:FF:FF`.

    Now, if the client does not know the destination MAC address, it sends an ARP request.
    It sends ARP requests with every second until it learns the address (up to 5 attempts).
    If it does not obtain the address, it falls back to the original strategy and sends
    the data as a broadcast.

    A new argument "/NO_ARP_REQUESTS" has been added to disable sending ARP requests.
    When used, the client behaves as before: it does not send ARP requests but still responds to them
    and learns from incoming queries.

    Disabling the sending of ARP requests is useful, for example, for communication via the SLIP
    packet driver. SLIP operates at the IP layer and does not use MAC addresses.

## Fixes

- Init ip_mac_map tbl before saving gw addr, add missing brackets

- Better check if receive buffer is free

## Other

- Code unification, removal of some magic constants

----

# 1.1.1 (2025-04-07)

## Fixes

- Store packet/request id/seq in resident memory

## Other

- Optimization: Do not use static local variables in function get_cds

----

# 1.1.0 (2025-04-06)

## Features

- Configurable response timeout.

    Added new arguments `/MIN_RCV_TMO:<seconds>` and `/MAX_RCV_TMO:<seconds>` to the `mount` command.

    The client sends a request and waits for a response for a minimum configured timeout.
    If no response is received, it sends the request again and again. The request can be resent
    up to 3 times (so it is sent up to 4 times in total). The timeout doubles with each retry
    up to the maximum configured timeout.

    The minimum and maximum timeout can be configured from 1 to 56 seconds. The default values are
    1 second for the minimum and 5 seconds for the maximum.

    Previously, the minimum timeout was hardcoded in code, about 55 - 110 milliseconds, and increased
    by 55 milliseconds with each retry. Which is usable on a "fast" network, but generally too aggressive.
    On a high latency network, or a very slow network (e.g. RS232 serial line), it can cause unnecessary
    repetition of requests and thus more load on the network.

- Configurable number of request retries.

    Added a new argument `/MAX_RETRIES:<count>` to the `mount` command.

    The value defines the maximum number of times to resend a request if no response is received.
    Supported values are 0 - 254. The default value is 4. Thus, the request can be retried 4 more times
    (5 sends in total).

    Previously, 3 retries (4 total sends) were hardcoded in the code.

## Fixes

- Check sequence number of response

- Add missing program exit on bad arguments

## Other

- Optimization: Print help with just one function call

----

# 1.0.0 (2025-04-01)

- First version
