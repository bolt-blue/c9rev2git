# Currently set up for debug release
CC=gcc
CFLAGS=-g -fstack-protector-all
LDLIBS=-lsqlite3

.PHONY: all
all: c9rev2git

c9rev2git: src/c9rev2git.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)
