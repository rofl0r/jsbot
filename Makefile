#CC = ~/musl-debug/bin/musl-gcc
CFLAGS = -DUSE_SSL -DUSE_CYASSL -static -Wall

-include config.mak

all:
	CFLAGS="$(CFLAGS)" rcb $(RCBFLAGS) --force --debug jsbot.c
