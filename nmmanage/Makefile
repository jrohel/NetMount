# Makefile for GNU make
# Uses Open Watcom wcl

all: nmmanage.com nmmanage.lst

nmmanage.obj: nmmanage.c ../shared/exitcode.h ../shared/nettypes.h ../shared/shdata.h
	wcc nmmanage.c -0 -w4 -e25 -zq -zastd=c99 -d1 -otexan -s -bt=dos -fo=$@ -ms

nmmanage.com: nmmanage.obj
	wlink name $@ d all sys dos com op m libf cstart_t.obj op maxe=25 op symf op el FILE nmmanage.obj

# disassemble
nmmanage.lst: nmmanage.obj
	wdis nmmanage.obj -s=nmmanage.c -l=$@

clean:
	rm -f nmmanage.com nmmanage.obj nmmanage.map nmmanage.sym nmmanage.lst
