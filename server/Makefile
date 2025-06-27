#######################
# Variables
#######################

CROSS_COMPILE=
DEFINES=
CC=$(CROSS_COMPILE)gcc -g -Wall -Wextra $(DEFINES)

#######################
# Targets
#######################

.PHONY: all clean

all: aesdsocket

clean:
	rm -rf aesdsocket

aesdsocket: server.c server_impl.c server_impl.h
	$(CC) -o $@ $^ -pthread -lrt
