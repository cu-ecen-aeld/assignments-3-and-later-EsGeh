#include "server_impl.h"


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


const int PORT = 9000;
const char* output_filename = "/var/tmp/aesdsocketdata";

_Atomic bool should_stop = false;

ret_t server_init(data_t* data)
{
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
		struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		// OUTPUT_DEBUG("select\n");
		fd_set available = read_set;
		int select_ret = select(
				data->socket_fd + 1,
				&available,
				NULL, NULL,
				&timeout
		);
		if( select_ret == -1 ) {
			if( errno == EINTR ) {
				// OUTPUT_DEBUG("interrupted, continue\n");
				continue;
			}
			OUTPUT_ERR("ERROR: select: %d - %s\n", errno, strerror(errno) );
			return RET_ERR;
		}
		if( ! FD_ISSET( data->socket_fd, &available ) ) {
			// OUTPUT_DEBUG("waiting...\n");
			if( should_stop ) {
				// OUTPUT_DEBUG("exit wait loop\n");
				return RET_OK;
			}
			continue;
		}
		OUTPUT_DEBUG( "accept\n" );
		struct sockaddr_in client_addr;
		uint addr_len = sizeof( struct sockaddr_in );
		data->client_socket_fd = accept(
				data->socket_fd,
				(struct sockaddr *) &client_addr,
				&addr_len
		);
		if( data->client_socket_fd == -1 ) {
			return RET_ERR;
		}
		OUTPUT_INFO( "Accepted connection from %s\n",
			inet_ntoa( client_addr.sin_addr )
		);

		FILE* socket_input = fdopen( data->client_socket_fd, "r" );
		int fd_copy = dup(data->client_socket_fd);
		FILE* socket_output = fdopen( fd_copy, "w" );
		if( RET_OK != server_protocol(
				socket_input,
				socket_output,
				data->output_file
		))
		{
			OUTPUT_ERR( "error talking with client\n" );
		}
		else {
			OUTPUT_INFO( "Closed connection from  %s\n",
				inet_ntoa( client_addr.sin_addr )
			);
		}
		// close client socket(s):
		{
			if( 0 != fclose( socket_input ) )
			{
				OUTPUT_ERR("ERROR: failed closing client_socket input\n" );
				return RET_ERR;
			}
			if( 0 != fclose( socket_output ) )
			{
				OUTPUT_ERR("ERROR: failed closing client_socket output\n" );
				return RET_ERR;
			}
			data->client_socket_fd = -1;
		}
	}
	return RET_OK;
}

const int BUFFER_SIZE = 256;

ret_t server_protocol(
		FILE* socket_input,
		FILE* socket_output,
		FILE* output_file
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
	OUTPUT_DEBUG( "server_exit\n" );
	// close:
	ret_t ret = RET_OK;
	if( data->socket_fd != -1 ) {
		if( close(data->socket_fd) == -1 ) {
			perror("socket");
			ret = RET_ERR;
		}
	}
	if( data->client_socket_fd != -1 ) {
		if( close(data->client_socket_fd) == -1 ) {
			perror("client_socket");
			ret = RET_ERR;
		}
	}
	if( data->output_file != NULL ) {
		if( 0 != fclose( data->output_file ) ) {
			perror(output_filename);
			ret = RET_ERR;
		}
	}
	return ret;
}
