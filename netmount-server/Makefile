CPPFLAGS = -O2 -W -Wall -Wextra -std=c++20

SOURCES = netmount-server.cpp fs.cpp udp_socket.cpp
HEADERS = fs.hpp udp_socket.hpp utils.hpp ../shared/dos.h ../shared/drvproto.h

all: netmount-server

netmount-server: $(SOURCES) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(SOURCES) -o $@

clean:
	$(RM) netmount-server
