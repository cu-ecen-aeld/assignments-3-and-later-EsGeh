#pragma once

#include <syslog.h>
#include <stdbool.h>
#include <stdio.h>


#define OUTPUT_ERR(fmt,...) syslog(LOG_ERR, fmt, ## __VA_ARGS__ )
// #define OUTPUT_ERR(fmt,...) fprintf(stderr, fmt, ## __VA_ARGS__ )
#define OUTPUT_INFO(fmt,...) syslog(LOG_INFO, fmt, ## __VA_ARGS__ )
// #define OUTPUT_INFO(fmt,...) fprintf(stdout, fmt, ## __VA_ARGS__ )
#define OUTPUT_DEBUG(fmt,...)
// #define OUTPUT_DEBUG(fmt,...) fprintf(stdout, fmt, ## __VA_ARGS__ )

typedef enum {
	RET_OK,
	RET_ERR,
} ret_t;

typedef struct {
	int socket_fd;
	int client_socket_fd;
	FILE* output_file;
} data_t;

typedef struct {
	bool demonize;
} args_t;

extern _Atomic bool should_stop;

ret_t server_init(data_t* data);
ret_t server_run(data_t* data);
ret_t server_exit(data_t* data);
ret_t server_protocol(
		FILE* socket_input,
		FILE* socket_output,
		FILE* output_file
);
