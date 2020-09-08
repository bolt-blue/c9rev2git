# Currently set up for debug release
CC=gcc
CFLAGS=-Og -fstack-protector-all
LDLIBS=-lsqlite3

.PHONY: all
all: c9rev2git

c9rev2git:
	$(CC) $(CFLAGS) -o $@ src/$@.c $(LDFLAGS) $(LDLIBS)
