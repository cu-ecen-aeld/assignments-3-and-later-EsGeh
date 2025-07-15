#include "server_impl.h"
#include "../aesd-char-driver/aesd_ioctl.h"


#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <time.h>

// sockets:
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


/***********************
 * Types
 ***********************/

typedef struct {
	FILE* output_file;
	pthread_mutex_t* output_file_mutex;
} clock_thread_info_t;

/***********************
 * Constants
 ***********************/

const int PORT = 9000;
const int BUFFER_SIZE = 256;
#ifdef USE_AESD_CHAR_DEVICE
const char* output_filename = "/dev/aesdchar";
#else
const char* output_filename = "/var/tmp/aesdsocketdata";
#endif

/***********************
 * Global Data
 ***********************/

_Atomic bool should_stop = false;

bool cleanup_thread_initialized = false;
pthread_t cleanup_thread_fd = -1;

clock_thread_info_t clock_thread_info;
bool clock_thread_initialized = false;
pthread_t clock_thread_fd = -1;
sem_t* clock_sem = NULL;

/***********************
 * Function Declarations
 ***********************/

void* client_thread_wrapper(void* void_arg);

ret_t client_thread(
		thread_info_t* thread_info
);

void* cleanup_thread_wrapper(void* void_arg);

ret_t cleanup_thread(
		thread_list_t* thread_list,
		sem_t* thread_finished_signal
);

void* clock_thread_wrapper(void* void_arg);
ret_t clock_thread(
	FILE* output_file,
	pthread_mutex_t* output_file_mutex
);

void timer_callback(int sig);

/***********************
 * Function Definitions
 ***********************/

void server_zero_data(data_t* data)
{
	(*data) = (data_t ){
		.socket_fd = -1,
		.output_file = NULL,
		.thread_finished_signal = NULL,
		.timer = NULL
	};
	TAILQ_INIT(&data->thread_list);
	pthread_mutex_init( &data->output_file_mutex, NULL );
}

ret_t server_init(data_t* data)
{
	// thread_finished_signal semaphore:
	data->thread_finished_signal = malloc(sizeof(sem_t));
	if( sem_init( data->thread_finished_signal, 0, 0 ) ) {
		perror( "sem_init" );
		FREE( data->thread_finished_signal );
		return RET_ERR;
	}
	clock_sem = malloc( sizeof(sem_t) );
	if( sem_init( clock_sem, 0, 0 ) ) {
		perror( "sem_init" );
		FREE( clock_sem );
		return RET_ERR;
	}
	// open output file
	{
		data->output_file = fopen(
				output_filename,
				"w+"
		);
		if( data->output_file == NULL ) {
			perror(output_filename);
			return RET_ERR;
		}
	}
	// cleanup_thread:
	{
		int ret = pthread_create(
				&cleanup_thread_fd,
				0,
				cleanup_thread_wrapper,
				data
		);
		if( ret != 0 ) {
			OUTPUT_ERR( "pthread_create: %d - %s\n", ret, strerror(ret) );
			return RET_ERR;
		}
		cleanup_thread_initialized = true;
	}
	// clock_thread:
	{
		clock_thread_info = (clock_thread_info_t ){
			.output_file = data->output_file,
			.output_file_mutex = &data->output_file_mutex,
		};
		int ret = pthread_create(
				&clock_thread_fd,
				0,
				clock_thread_wrapper,
				&clock_thread_info
		);
		if( ret != 0 ) {
			OUTPUT_ERR( "pthread_create: %d - %s\n", ret, strerror(ret) );
			return RET_ERR;
		}
		clock_thread_initialized = true;
	}
	// create socket:
	OUTPUT_DEBUG( "socket\n" );
	{
		data->socket_fd = socket(
				PF_INET, 			// IPv4 
				SOCK_STREAM,	// TCP
				0
		);
		if( data->socket_fd == -1 ) {
			perror("socket");
			return RET_ERR;
		}
		// make port reusable:
		const int y = 1;
		setsockopt(
				data->socket_fd,
				SOL_SOCKET,
				SO_REUSEADDR, &y, sizeof(int)
		);
	}
	// bind:
	OUTPUT_DEBUG( "bind\n" );
	{
		struct sockaddr_in addr;
		memset( &addr, 0, sizeof(addr) );
		addr.sin_family = AF_INET; 
		addr.sin_addr.s_addr = INADDR_ANY; 
		addr.sin_port = htons( PORT );
		// Binding newly created socket to given IP and verification 
		if( bind(
					data->socket_fd,
					(struct sockaddr *)&addr,
					sizeof(addr)
		) != 0 )
		{
			perror("socket");
			return RET_ERR;
		} 
	}
	// listen:
	OUTPUT_DEBUG( "listen\n" );
	if( listen( data->socket_fd, 5 ) == -1 ) {
		perror("socket");
		return RET_ERR;
	}
	signal( SIGALRM, timer_callback );
	struct itimerspec timer_spec = {
		.it_value.tv_sec = 10,
		.it_value.tv_nsec = 0,
		.it_interval.tv_sec = 10,
		.it_interval.tv_nsec = 0,
	};
	data->timer = malloc( sizeof(timer_t) );
	if( timer_create( CLOCK_REALTIME, 0, data->timer) ) {
		perror("timer_create");
		return RET_ERR;
	}
	if( timer_settime( *(data->timer), 0, &timer_spec, NULL) ) {
		perror("timer_settime");
		return RET_ERR;
	}
	// syslog( LOG_INFO, "listening...\n" );
	return RET_OK;
}

ret_t server_run(data_t* data)
{
	fd_set read_set;
	FD_ZERO( &read_set );
	FD_SET( data->socket_fd, &read_set );
	// server:
	while( true )
	{
		// OUTPUT_DEBUG("select\n");
		fd_set available = read_set;
		int select_ret = select(
				data->socket_fd + 1,
				&available,
				NULL, NULL,
				NULL
		);
		if( select_ret == -1 ) {
			if( errno == EINTR ) {
				if( should_stop ) {
					// OUTPUT_DEBUG("exit wait loop\n");
					return RET_OK;
				}
				// OUTPUT_DEBUG("interrupted, continue\n");
				continue;
			}
			OUTPUT_ERR("ERROR: select: %d - %s\n", errno, strerror(errno) );
			return RET_ERR;
		}
		OUTPUT_DEBUG( "accept\n" );
		thread_info_t* thread_info = malloc( sizeof(thread_info_t) );
		thread_info->thread_finished = false;
		thread_info->output_file = data->output_file;
		thread_info->output_file_mutex = &data->output_file_mutex;
		thread_info->thread_finished_signal = data->thread_finished_signal;
		// struct sockaddr_in client_addr;
		uint addr_len = sizeof( struct sockaddr_in );
		int client_socket_fd = accept(
				data->socket_fd,
				(struct sockaddr *) &thread_info->client_addr,
				&addr_len
		);
		if( client_socket_fd == -1 ) {
			return RET_ERR;
		}
		OUTPUT_INFO( "Accepted connection from %s\n",
			inet_ntoa( thread_info->client_addr.sin_addr )
		);

		thread_info->socket_input = fdopen( client_socket_fd, "r" );
		int fd_copy = dup( client_socket_fd );
		thread_info->socket_output = fdopen( fd_copy, "w" );
		{
			int ret = pthread_create(
					&thread_info->thread_fd,
					0,
					client_thread_wrapper,
					thread_info
			);
			if( ret != 0 ) {
				OUTPUT_ERR( "pthread_create: %d - %s\n", ret, strerror(ret) );
				return RET_ERR;
			}
		}
	}
	return RET_OK;
}

void server_stop(data_t* data)
{
	should_stop = true;
	sem_post( clock_sem );
}

void* client_thread_wrapper(void* void_arg)
{
	thread_info_t* arg = (thread_info_t* )void_arg;
	arg->ret = client_thread( arg );
	return &arg->ret;
}

ret_t client_thread(
		thread_info_t* thread_info
)
{
	ret_t ret = RET_OK;
	pthread_mutex_lock( thread_info->output_file_mutex );
	if( RET_OK != server_protocol(
			thread_info->socket_input,
			thread_info->socket_output,
			thread_info->output_file
			// thread_info->output_file_mutex
	))
	{
		OUTPUT_ERR( "error talking with client\n" );
		ret = RET_ERR;
	}
	else {
		OUTPUT_INFO( "Closed connection from  %s\n",
			inet_ntoa( thread_info->client_addr.sin_addr )
		);
	}
	pthread_mutex_unlock( thread_info->output_file_mutex );
	// close client socket(s):
	{
		if( 0 != fclose( thread_info->socket_input ) )
		{
			OUTPUT_ERR("ERROR: failed closing client_socket input\n" );
			ret = RET_ERR;
		}
		if( 0 != fclose( thread_info->socket_output ) )
		{
			OUTPUT_ERR("ERROR: failed closing client_socket output\n" );
			ret = RET_ERR;
		}
	}
	// signalize the cleanup thread, that we are done:
	thread_info->thread_finished = true;
	sem_post( thread_info->thread_finished_signal );
	return ret;
}

void* cleanup_thread_wrapper(void* void_arg)
{
	data_t* data = (data_t* )void_arg;
	static ret_t ret;
	ret = cleanup_thread(
			&data->thread_list,
			data->thread_finished_signal
	);
	return &ret;
}

ret_t cleanup_thread(
		thread_list_t* thread_list,
		sem_t* thread_finished_signal
)
{
	OUTPUT_DEBUG( "cleanup_thread: START\n" );
	while(true) {
		if( sem_wait(thread_finished_signal) ) {
			perror("sem_wait");
			return RET_ERR;
		}
		OUTPUT_DEBUG( "cleanup_thread: run...\n" );
		// search for the finished thread
    thread_info_t* thread_info = NULL;
    thread_info_t* current_node = NULL;
    TAILQ_FOREACH(current_node, thread_list, nodes) {
			if( current_node->thread_finished ) {
				thread_info = current_node;
			}
		}
		if( thread_info == NULL ) {
			OUTPUT_DEBUG( "cleanup_thread: STOP\n" );
			return RET_OK;
		}
		// join thread and delete corresponding list node:
		OUTPUT_DEBUG( "cleanup_thread: join client thread\n" );
		ret_t* thread_ret;
		if( pthread_join( thread_info->thread_fd, (void** )&thread_ret ) ) {
			perror( "pthread_join" );
		}
		TAILQ_REMOVE(thread_list, thread_info, nodes);
		FREE(thread_info);
	}
}

void* clock_thread_wrapper(void* void_arg)
{
	clock_thread_info_t* arg = (clock_thread_info_t* )void_arg;
	static ret_t ret;
	ret = clock_thread(
			arg->output_file,
			arg->output_file_mutex
	);
	return &ret;
}

ret_t clock_thread(
	FILE* output_file,
	pthread_mutex_t* output_file_mutex
)
{
	char buffer[BUFFER_SIZE];
  time_t current_time;
	OUTPUT_DEBUG( "clock_thread: START\n" );
	while(true) {
		if( sem_wait( clock_sem ) ) {
			perror("sem_wait");
			return RET_ERR;
		}
		if( should_stop ) {
			OUTPUT_DEBUG( "clock_thread: STOP\n" );
			return RET_OK;
		}
#ifdef USE_AESD_CHAR_DEVICE
		OUTPUT_DEBUG( "clock_thread: TICK\n" );
		current_time = time(NULL);
		struct tm* local_time = localtime(&current_time);
		if( local_time == NULL ) {
			perror("localtime");
			return RET_ERR;
		}
		strftime(
				buffer,
				BUFFER_SIZE,
				"timestamp:%a, %d %b %Y %T %z\n",
				local_time
		);
		OUTPUT_DEBUG( "clock_thread: WRITE '%s'", buffer );
		pthread_mutex_lock(output_file_mutex);
		{
			int length = strlen( buffer );
			int write_ret = fwrite(buffer, sizeof(char), length, output_file );
			if( length*sizeof(char) != (size_t )write_ret ) {
				pthread_mutex_unlock(output_file_mutex);
				OUTPUT_ERR( "ERROR: failed writing to output file\n" );
				return RET_ERR;
			}
			// OUTPUT_DEBUG( "flush\n" );
			fflush( output_file );
		}
		pthread_mutex_unlock(output_file_mutex);
		OUTPUT_DEBUG( "clock_thread: WRITE done\n" );
#endif
	}
}

ret_t server_protocol(
		FILE* socket_input,
		FILE* socket_output,
		FILE* output_file
		// // pthread_mutex_t* output_file_mutex
)
{
	char buffer[BUFFER_SIZE];
	void* fgets_ret;
	// read from socket, write to file:
	while( true ) {
		fgets_ret = fgets( buffer, BUFFER_SIZE, socket_input );
		if( fgets_ret == NULL ) {
			if( !feof(socket_input) ) {
				OUTPUT_ERR( "error reading socket\n" );
				return RET_ERR;
			}
			else {
				OUTPUT_ERR( "missing newline\n" );
				return RET_ERR;
			}
		}
		int length = strlen( buffer );
		OUTPUT_DEBUG( "received %d bytes\n", length );
		// AESDCHAR_IOCSEEKTO:X,Y
		{
			const char* prefix = "AESDCHAR_IOCSEEKTO";
			const int prefix_length = strlen( prefix );
			if(
					!strncmp( prefix, buffer, strlen(prefix) )
					&& length > prefix_length
					&& buffer[prefix_length] == ':'
					&& (strchr( &buffer[prefix_length+1], ',' ) != NULL)
			) {
				OUTPUT_DEBUG( "AESDCHAR_IOCSEEKTO found!\n" );
				int x = 0;
				char* endptr = NULL;
				char* current_str = &buffer[prefix_length+1];
				x = strtol( current_str, &endptr, 10);
				if( endptr != current_str && endptr[0] == ',' ) {
					current_str = endptr+1;
					int y = strtol( current_str, &endptr, 10);
					if( endptr != current_str && endptr[0] == '\0' ) {
						struct aesd_seekto seek_to = {
							.write_cmd = x,
							.write_cmd_offset = y,
						};
						if( !ioctl(
								fileno(output_file),
								AESDCHAR_IOCSEEKTO,
								&seek_to
						) ) {
							OUTPUT_ERR( "ERROR: ioctl failed with: %d - '%s'\n", errno, strerror(errno) );
							return RET_ERR;
						}
						continue;
					}
				}
			}
		}
		int write_ret = fwrite(buffer, sizeof(char), length, output_file );
		if( length*sizeof(char) != (size_t )write_ret ) {
			OUTPUT_ERR( "ERROR: failed writing to output file\n" );
			return RET_ERR;
		}
		// OUTPUT_DEBUG( "flush\n" );
		fflush( output_file );
		if( buffer[length-1] == '\n' ) {
			// OUTPUT_DEBUG( "break\n" );
			break;
		}
	}
	// read from file, write to socket:
	OUTPUT_DEBUG( "rewind\n" );
	rewind(output_file);
	// write output_file to socket:
	while( NULL != fgets( buffer, BUFFER_SIZE, output_file ) ) {
		int length = strlen( buffer );
		OUTPUT_DEBUG( "writing %d bytes to socket\n", length );
		if( length*sizeof(char) != fwrite(buffer, sizeof(char), length, socket_output ) ) {
			OUTPUT_ERR( "ERROR: failed writing to socket\n" );
			return RET_ERR;
		}
	}
	if( !feof( output_file ) ) {
		OUTPUT_ERR( "ERROR: failed reading output file\n" );
		return RET_ERR;
	}
	return RET_OK;
}

ret_t server_exit(data_t* data)
{
	ret_t ret = RET_OK;
	OUTPUT_DEBUG( "server_exit\n" );
	if( data->timer != NULL ) {
		if( timer_delete( *(data->timer) ) ) {
			perror("timer_delete");
			ret = RET_ERR;
		}
		FREE( data->timer );
	}
	if( cleanup_thread_initialized ) {
		ret_t* cleanup_ret;
		OUTPUT_DEBUG( "join cleanup_thread\n" );
		sem_post( data->thread_finished_signal );
		if( pthread_join( cleanup_thread_fd, (void** )&cleanup_ret) ) {
			perror( "pthread_join" );
		}
	}
	if( clock_thread_initialized ) {
		ret_t* clock_ret;
		OUTPUT_DEBUG( "join clock_thread\n" );
		if( pthread_join( clock_thread_fd, (void** )&clock_ret) ) {
			perror( "pthread_join" );
		}
	}
	// socket:
	if( data->socket_fd != -1 ) {
		if( close(data->socket_fd) == -1 ) {
			perror("socket");
			ret = RET_ERR;
		}
	}
	// output file:
	if( data->output_file != NULL ) {
		if( 0 != fclose( data->output_file ) ) {
			perror(output_filename);
			ret = RET_ERR;
		}
	}
	// output_file_mutex:
	{
		int err_code = pthread_mutex_destroy( &data->output_file_mutex );
		if( err_code != 0 ) {
			OUTPUT_ERR( "ERROR: 'pthread_mutex_destroy': %d - %s\n", err_code, strerror(err_code) );
			ret = RET_ERR;
		}
	}
	if( data->thread_finished_signal != NULL ) {
		if( sem_destroy( data->thread_finished_signal ) ) {
			perror( "sem_destroy" );
			ret = RET_ERR;
		}
		FREE( data->thread_finished_signal );
	}
	if( clock_sem != NULL ) {
		if( sem_destroy( clock_sem ) ) {
			perror( "sem_destroy" );
			ret = RET_ERR;
		}
		FREE( clock_sem );
	}
	// free queue:
	{
		thread_info_t * e = NULL;
		if( !TAILQ_EMPTY(&data->thread_list) ) {
			OUTPUT_ERR("thread_list is not empty");
		}
		while (!TAILQ_EMPTY(&data->thread_list))
		{
			e = TAILQ_FIRST(&data->thread_list);
			TAILQ_REMOVE(&data->thread_list, e, nodes);
			free(e);
			e = NULL;
		}	
	}
#ifdef USE_AESD_CHAR_DEVICE
	if( unlink( output_filename ) ) {
		perror(output_filename);
		ret = RET_ERR;
	}
#endif
	return ret;
}

void timer_callback(int sig)
{
	if( clock_sem ) {
		sem_post( clock_sem );
	}
}
