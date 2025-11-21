# Makefile for GNU make
# Uses Open Watcom wcl

all: ninstall.exe

ninstall.exe: ninstall.c
	wcl -0 -w4 -e25 -zq -zastd=c99 -otexan -bt=dos -l=dos -ms ninstall.c

clean:
	rm -f ninstall.exe ninstall.o
