#######################
# Variables
#######################

CROSS_COMPILE=
CC=$(CROSS_COMPILE)gcc -Wall -Wextra

#######################
# Targets
#######################

.PHONY: all clean

all: aesdsocket

clean:
	rm -rf aesdsocket

aesdsocket: server.c server_impl.c server_impl.h
	$(CC) -o $@ $^
