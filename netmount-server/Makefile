CPPFLAGS = -O2 -W -Wall -Wextra -std=c++20

SOURCES = netmount-server.cpp fs.cpp udp_socket.cpp slip_udp_serial.cpp serial_port.cpp
HEADERS = fs.hpp udp_socket.hpp slip_udp_serial.hpp serial_port.hpp utils.hpp ../shared/dos.h ../shared/drvproto.h

all: netmount-server

netmount-server: $(SOURCES) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(SOURCES) -o $@

clean:
	$(RM) netmount-server
