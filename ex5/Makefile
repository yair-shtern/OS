CC=g++
RANLIB=ranlib

SRC=container.cpp sockets.cpp

CFLAGS=-Wall -std=c++11

TARGETS=container.o sockets.o

TAR=tar
TARFLAGS=-cvf
TARNAME=ex5.tar
TARSRCS=$(SRC) Makefile README

all:make1 make2

make1:
	$(CC) $(CFLAGS) container.cpp -o container

make2:
	$(CC) $(CFLAGS) sockets.cpp -o sockets

clean:
	$(RM) $(TARGETS) *~ *core

tar:
	$(TAR) $(TARFLAGS) $(TARNAME) $(TARSRCS)
