# Currently set up for debug release
CC=gcc
CFLAGS=-g -fstack-protector-all -DDEBUG
LDLIBS=-lgit2 -lsqlite3

# ref: https://libgit2.org/docs/guides/build-and-link/
LDFLAGS += $(shell pkg-config --libs libgit2)
CFLAGS += $(shell pkg-config --cflags libgit2)

.PHONY: all
all: c9rev2git

c9rev2git: src/c9rev2git.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)
