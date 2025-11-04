# 1.7.0 (2025-11-19)

## Fixes

- **pktdrv_init: Fix releasing packet driver handle for ARP frames**

    There was a bug in the argument: the type of ARP frame was passed
    instead of the handle. The code is executed only in the event of
    a packet driver initialization failure, and only when the handler for
    ARP frames was successfully registered but the registration of the IPv4
    handler subsequently failed. This scenario normally does not occur.

## Other

- **Remove redundant GPL-2.0 license file**

- **Block reception of frames until netmount is fully initialized**

- **Add checks when retrieving local MAC address from packet driver**

- **Add NetMount client and ABI version info to shared data**

    Prepares the shared data structure for use by external programs
    that will interact with the NetMount client.

- **Restrict packet driver interrupt range to 0x60–0x80**

    Previously, the valid range was 0x60–0xFF. The upper limit was reduced
    to 0x80 to match the range of software interrupts used by DOS packet
    drivers (0x60–0x80).

- **Define and use new/renamed configuration macros**

- **Check installed NetMount version for compatibility**

- **Move shared_data struct and parameter macros to shdata.h**

    Moved the definition of `struct shared_data` and the NetMount client
    parameter macros from netmount.c to shdata.h.

    This allows other programs (e.g. the upcoming NetMount management tool)
    to access shared_data and related configuration limits (min/max/default)
    without duplicating definitions.

- **Move additional header files to "shared" directory**

    Moved shdata.h, exitcode.h, nettypes.h, and pktdrv.h into the existing
    "shared" directory, which already contains headers shared with the
    NetMount server. This makes these headers available to other programs
    in the future (e.g., the upcoming NetMount management tool).

----

# 1.6.0 (2025-10-12)

## Features

- **Support hexadecimal numbers in arguments**

    NetMount still accepts numbers in decimal by default, but if a number in an argument starts
    with `"0x"` or `"0X"`, it is now interpreted as a hexadecimal number.

- **Read-ahead buffering for file reads**

    By default, it uses a 64-byte buffer and, when reading files, requests at least 64 bytes
    from the server to fill this buffer - assuming that subsequent reads will be sequential and
    can be served directly from it. This change results in up to 64x faster reads when
    the application reads 1 byte at a time, and 16x faster for 4-byte blocks. Read-ahead buffering
    also reduces the load on the network and the NetMount server. Serving a single 64-byte read is
    much less demanding than handling 16 separate 4-byte requests, or 64 one-byte requests.

    The size of the read-ahead buffer is configurable using the `/MIN_READ_LEN:<length>` argument.
    The `<length>` value may be one of the following: `0`, `1`, `2`, `4`, `8`, `16`, `32`, or `64`.
    Setting it to `0` disables read-ahead buffering entirely.
    If the `/MIN_READ_LEN:<length>` argument is not provided, the default value of `64` is used.
    This setting is independent for each mounted drive. However, internally, the NetMount client uses
    a single shared 64-byte read-ahead buffer for all mounted drives, and the setting determines
    how many bytes from the beginning of the buffer are used by each drive.

    Read-ahead buffering is applied only when the read request is smaller than the configured buffer
    size. Larger reads bypass the buffer.

## Other

- **Optimize memory usage**

    Release environment from memory, and do it during the INSTALL phase

- **Extend validation of arguments**

- **Add 8086/80286 targets to Makefile and disassembly**

- **Add documentation section on sharing a network interface with other applications**

----

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
