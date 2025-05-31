#######################
# Variables
#######################

CROSS_COMPILE=
CC=$(CROSS_COMPILE)gcc

#######################
# Targets
#######################

.PHONY: all clean

all: writer

clean:
	rm -rf writer

writer: writer.c
	$(CC) -o $@ $<
