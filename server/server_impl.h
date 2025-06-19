#pragma once

#include <syslog.h>
#include <stdbool.h>
#include <stdio.h>

// posix threads:
#include <pthread.h>
// posix semaphores:
#include <semaphore.h>

// Queues
#include <sys/queue.h>

// sockets:
#include <netinet/in.h>


#define OUTPUT_ERR(fmt,...) syslog(LOG_ERR, fmt, ## __VA_ARGS__ )
// #define OUTPUT_ERR(fmt,...) fprintf(stderr, fmt, ## __VA_ARGS__ )
#define OUTPUT_INFO(fmt,...) syslog(LOG_INFO, fmt, ## __VA_ARGS__ )
// #define OUTPUT_INFO(fmt,...) fprintf(stdout, fmt, ## __VA_ARGS__ )
#ifndef DEBUG_OUTPUT
#define OUTPUT_DEBUG(fmt,...)
#else
#define OUTPUT_DEBUG(fmt,...) fprintf(stdout, fmt, ## __VA_ARGS__ )
#endif

#define FREE(p) { \
	if( p != NULL ) { \
		free(p); \
		p = NULL; \
	} \
}

/***********************
 * Types
 ***********************/

typedef enum {
	RET_OK,
	RET_ERR,
} ret_t;

typedef struct thread_info {
	pthread_t thread_fd;
	struct sockaddr_in client_addr;
	FILE* socket_input;
	FILE* socket_output;
	FILE* output_file;
	pthread_mutex_t* output_file_mutex;
	sem_t* thread_finished_signal;
	bool thread_finished;
	ret_t ret;
	// 
	TAILQ_ENTRY(thread_info) nodes;
} thread_info_t;

// This typedef creates a head_t that makes it easy for us to pass pointers to
// head_t without the compiler complaining.
typedef TAILQ_HEAD(head_s, thread_info) thread_list_t;

typedef struct {
	int socket_fd;
	FILE* output_file;
	pthread_mutex_t output_file_mutex;
	sem_t* thread_finished_signal;
	thread_list_t thread_list;
} data_t;

typedef struct {
	bool demonize;
} args_t;

/***********************
 * Global Data
 ***********************/

extern _Atomic bool should_stop;

/***********************
 * Function Declarations
 ***********************/

void server_zero_data(data_t* data);
ret_t server_init(data_t* data);
ret_t server_run(data_t* data);
ret_t server_exit(data_t* data);
ret_t server_protocol(
		FILE* socket_input,
		FILE* socket_output,
		FILE* output_file
		// pthread_mutex_t* output_file_mutex
);
