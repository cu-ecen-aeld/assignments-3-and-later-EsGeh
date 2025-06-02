#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
// #define DEBUG_LOG(msg,...)
#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)


void* threadfunc(void* thread_param)
{
	// wait, obtain mutex, wait, release mutex as described by thread_data structure
	// obtain thread arguments from your parameter
	struct thread_data* thread_data = (struct thread_data *) thread_param;
	thread_data->thread_complete_success = true;
	DEBUG_LOG("sleep %dms", thread_data->wait_to_obtain_ms);
	if( usleep( thread_data->wait_to_obtain_ms * 1000 ) ) {
		perror("usleep");
		thread_data->thread_complete_success = false;
		return thread_param;
	}
	DEBUG_LOG("lock: taking...");
	if( pthread_mutex_lock(thread_data->mutex) ) {
		perror("pthread_mutex_lock");
		thread_data->thread_complete_success = false;
		return thread_param;
	}
	DEBUG_LOG("sleep %dms", thread_data->wait_to_release_ms);
	if( usleep( thread_data->wait_to_release_ms * 1000 ) ) {
		perror("usleep");
		thread_data->thread_complete_success = false;
		// don't return here from critical section
		// without releasing the lock!
	}
	if( pthread_mutex_unlock(thread_data->mutex) ) {
		perror("pthread_mutex_unlock");
		thread_data->thread_complete_success = false;
		return thread_param;
	}
	DEBUG_LOG("lock: released");
	return thread_param;
}

bool start_thread_obtaining_mutex(
		pthread_t *thread,
		pthread_mutex_t *mutex,
		int wait_to_obtain_ms,
		int wait_to_release_ms
)
{
/**
 * allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
 * using threadfunc() as entry point.
 *
 * return true if successful.
 *
 * See implementation details in threading.h file comment block
 */
	struct thread_data* thread_data = malloc(sizeof( struct thread_data));
	if( thread_data == NULL ) {
		perror("malloc");
		return false;
	}
	thread_data->mutex = mutex;
	thread_data->wait_to_obtain_ms = wait_to_obtain_ms;
	thread_data->wait_to_release_ms = wait_to_release_ms;
	DEBUG_LOG("start thread");
	if( 0 != pthread_create(
				thread,
				NULL,
				&threadfunc,
				thread_data
	) ) {
		perror( "pthread_create" );
		return false;
	}
	/* The caller should
	 * join the thread & free thread_data
	DEBUG_LOG("wait for thread");
	if( 0 != pthread_join(
				*thread, 
				NULL
	) ) {
		perror( "pthread_join" );
		return false;
	}
	bool ret = thread_data->thread_complete_success;
	free( thread_data );
	return ret;
	*/
	return true;
}
