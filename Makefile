ROCKSOCK_HEADERS = ../rocksock
RSIRC_HEADERS = ../rocksock/rocksockirc
INCLUDES = -I $(ROCKSOCK_HEADERS) -I $(RSIRC_HEADERS)

ifeq ($(SSL),CYASSL))
SSL_CFLAGS=-DUSE_CYASSL
else
SSL_CFLAGS=-DUSE_OPENSSL
endif

CFLAGS = $(INCLUDES) -DUSE_SSL $(SSL_CFLAGS) -static -Wall

# you may override or append to, CFLAGS, header locations,
# RCBFLAGS etc with a custom config.mak file...

-include config.mak

all:
	CFLAGS="$(CFLAGS)" rcb2 $(RCBFLAGS) jsbot.c
