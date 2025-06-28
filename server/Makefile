#######################
# Variables
#######################

CROSS_COMPILE=
DEFINES=

CFLAGS ?= -g -Wall -Wextra

CC ?= $(CROSS_COMPILE)gcc $(DEFINES)

LDFLAGS ?= -lpthread -lrt

#######################
# Targets
#######################

.PHONY: all clean

all: aesdsocket

clean:
	rm -rf aesdsocket

aesdsocket: server.c server_impl.c server_impl.h
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
