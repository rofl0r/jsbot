ROCKSOCK_HEADERS = ../rocksock
RSIRC_HEADERS = ../rocksock/rocksockirc
INCLUDES = -I $(ROCKSOCK_HEADERS) -I $(RSIRC_HEADERS)

CFLAGS = $(INCLUDES) -DUSE_SSL -DUSE_CYASSL -static -Wall

# you may override or append to, CFLAGS, header locations,
# RCBFLAGS etc with a custom config.mak file...

-include config.mak

all:
	CFLAGS="$(CFLAGS)" rcb $(RCBFLAGS) jsbot.c
