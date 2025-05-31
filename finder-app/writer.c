#include <syslog.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

void log_init(void);
void log_exit(void);

int main(int argc, char* argv[])
{
	log_init();

	if( argc != 3 ) {
		syslog( LOG_ERR, "usage: $0 FILE STR" );
		log_exit();
		return EXIT_FAILURE;
	}

	char* filename = argv[1];
	char* str = argv[2];

	{
		FILE* fp = fopen( filename, "w+" );
		if( fp == NULL ) {
			syslog( LOG_ERR, "'%s': %s", filename, strerror(errno) );
			return EXIT_FAILURE;
		}

		syslog( LOG_DEBUG, "Writing %s to %s", str, filename );
		fwrite( str, sizeof(char), strlen(str), fp);

		if( fclose( fp ) != 0 ) {
			syslog( LOG_ERR, "'%s': %s", filename, strerror(errno) );
			return EXIT_FAILURE;

		}
	}

	log_exit();
	return EXIT_SUCCESS;
}

void log_init(void)
{
	openlog( "writer", 0, LOG_USER );
}

void log_exit(void)
{
	closelog();
}
