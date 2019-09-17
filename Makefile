
prefix=/usr/local

all: osxsnarf

osxsnarf: osxsnarf.o
	9 9l -o osxsnarf osxsnarf.o

osxsnarf.o: osxsnarf.c
	9 9c osxsnarf.c

install: osxsnarf
	mkdir -p $(prefix)/bin
	cp osxsnarf $(prefix)/bin/

.PHONY: all install
