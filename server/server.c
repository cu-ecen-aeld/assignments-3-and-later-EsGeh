#include "server_impl.h"


#include <syslog.h>
#include <stdbool.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>

// sockets:
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


const char short_options[] = "hd";
const  struct option long_options[] = {
	{ "help", no_argument, 0, 'h' },
	{ "demonize", no_argument, 0, 'd' },
	{ 0,0,0,0 },
};

void log_init(void);
void log_exit(void);

void print_cmd_line_info(
		char* argv[]
);
int parse_cmd_line_args(
		int argc,
		char* argv[],
		args_t* args
);


void int_handler(int );

int main(int argc, char* argv[])
{
	log_init();

	data_t data;
	server_zero_data(&data);
	args_t args = {
		.demonize = false,
	};
	// parse cmd line args:
	{
		int ret = parse_cmd_line_args(
				argc, argv,
				&args
		);
		// --help:
		if( ret == -1 ) {
			print_cmd_line_info( /*argc,*/ argv );
			return EXIT_SUCCESS;
		}
		// error parsing cmd line args:
		else if( ret != 0 ) {
			print_cmd_line_info( /*argc,*/ argv );
			return EXIT_FAILURE;
		}
	}
	OUTPUT_INFO("-----------------------\n");
	OUTPUT_INFO("OPTIONS:\n");
	OUTPUT_INFO("demonize: %d\n", args.demonize);
	OUTPUT_INFO("-----------------------\n");
	if( args.demonize ) {
		int child_pid = fork();
		if( child_pid != 0 ) {
			OUTPUT_INFO( "server process id: %d\n", child_pid );
			return EXIT_SUCCESS;
		}
	}
	signal(SIGINT, int_handler);
	signal(SIGTERM, int_handler);
	if( RET_OK != server_init(&data) ) {
		server_exit(&data);
		return EXIT_FAILURE;
	}
	if( RET_OK != server_run(&data) ) {
		server_exit(&data);
		log_exit();
		return EXIT_FAILURE;
	}
	if( RET_OK != server_exit(&data) ) {
		server_exit(&data);
		log_exit();
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

void int_handler(int)
{
	// OUTPUT_DEBUG("interrupt\n");
	should_stop = true;
}

void log_init(void)
{
	openlog( "server", 0, LOG_USER );
}

void log_exit(void)
{
	closelog();
}

void print_cmd_line_info(
		char* argv[]
)
{
	printf( "usage: %s [OPTIONS]\n", argv[0] );
	printf( "\n" );
	printf( "OPTIONS\n" );
	printf(
			"%-16s: print help\n",
			"--help|-h"
	);
	printf(
			"%-16s: run in background as a demon process\n",
			"--demonize|-d"
	);
}

int parse_cmd_line_args(
		int argc,
		char* argv[],
		args_t* args
)
{
	// parse options:
	while( true ) {
		int option_index = 0;
		int c = getopt_long(
				argc, argv,
				short_options,
				long_options,
				&option_index
		);
		if( c == -1 ) { break; }
		switch( c ) {
			case 'h':
				return -1;
			break;
			case 'd':
				args->demonize = true;
			break;
			default:
				return 1;
		}
	}
	return 0;
}
