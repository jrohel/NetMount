# Makefile for GNU make
# For compilation and linking use Open Watcom wcc and wlink

all: netmount_286.com netmount_086.com netmount_286.lst netmount_086.lst

netmount_286.obj: netmount.c i86.h ../shared/dos.h ../shared/drvproto.h ../shared/exitcode.h ../shared/nettypes.h ../shared/pktdrv.h ../shared/shdata.h
	wcc netmount.c -2 -w4 -e25 -zq -zastd=c99 -d1 -otexan -s -bt=dos -fo=$@ -ms

netmount_086.obj: netmount.c i86.h ../shared/dos.h ../shared/drvproto.h ../shared/exitcode.h ../shared/nettypes.h ../shared/pktdrv.h ../shared/shdata.h
	wcc netmount.c -0 -w4 -e25 -zq -zastd=c99 -d1 -otexan -s -bt=dos -fo=$@ -ms

netmount_286.com: netmount_286.obj
	wlink name $@ d all sys dos com op m libf cstart_t.obj op maxe=25 op symf op el FILE netmount_286.obj

netmount_086.com: netmount_086.obj
	wlink name $@ d all sys dos com op m libf cstart_t.obj op maxe=25 op symf op el FILE netmount_086.obj

# disassemble
netmount_286.lst: netmount_286.obj
	wdis netmount_286.obj -s=netmount.c -l=$@

netmount_086.lst: netmount_086.obj
	wdis netmount_086.obj -s=netmount.c -l=$@

clean:
	rm -f netmount_286.* netmount_086.*
