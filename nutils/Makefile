# Makefile for GNU make
# Uses Open Watcom wcl

all: nutils.com nutils.lst

nutils.obj: nutils.c ../include/exitcode.h ../include/nettypes.h ../include/shdata.h
	wcc nutils.c -0 -w4 -e25 -zq -zastd=c99 -d1 -otexan -s -bt=dos -fo=$@ -ms

nutils.com: nutils.obj
	wlink name $@ d all sys dos com op m libf cstart_t.obj op maxe=25 op symf op el FILE nutils.obj

# disassemble
nutils.lst: nutils.obj
	wdis nutils.obj -s=nutils.c -l=$@

clean:
	rm -f nutils.com nutils.obj nutils.map nutils.sym nutils.lst
