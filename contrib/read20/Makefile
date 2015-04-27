# Quick and dirty makefile for read20.
# Invoke with appropriate system, e.g. "make freebsd"

SRCS= dump.h read20.c
OBJS= read20.o

usage:
	@echo 'Use appropriate system as target, eg "make freebsd"'

freebsd:
	cc -g -O -o read20 read20.c -lcompat

osfaxp:
	cc -g -O -std1 -o read20 read20.c

linux:
	cc -g -O -o read20 read20.c

solaris:
	gcc -g -O -o read20 read20.c

solaris-cc:
	/opt/SUNWspro/bin/cc -g -O -o read20 read20.c
