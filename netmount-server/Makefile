CPP = c++
CPPFLAGS = -O2 -W -Wall -Wextra -std=c++20

all: netmount-server

netmount-server: netmount-server.cpp fs.cpp fs.hpp udp_socket.cpp udp_socket.hpp utils.hpp ../shared/dos.h ../shared/drvproto.h
	$(CPP) $(CPPFLAGS) netmount-server.cpp fs.cpp udp_socket.cpp -o netmount-server

clean:
	rm -f netmount-server
