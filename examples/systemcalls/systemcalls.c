#include "systemcalls.h"

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
 */
bool do_system(const char *cmd)
{
	/*
	 *  Call the system() function with the command set in the cmd
	 *   and return a boolean true if the system() call completed with success
	 *   or false() if it returned a failure
	 */
	int ret = system(cmd);
	if( ret == -1 ) {
		perror( "system" );
		return false;
	}
	if( WIFEXITED(ret) ) {
		if( WEXITSTATUS(ret) != EXIT_SUCCESS ) {
			return false;
		}
		return true;
	}
	return false;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
	va_list args;
	va_start(args, count);
	char* command[count+1];
	for(int i=0; i<count; i++)
	{
		command[i] = va_arg(args, char *);
	}
	va_end(args);
	command[count] = NULL;

	/*
	 *   Execute a system command by calling fork, execv(),
	 *   and wait instead of system (see LSP page 161).
	 *   Use the command[0] as the full path to the command to execute
	 *   (first argument to execv), and use the remaining arguments
	 *   as second argument to the execv() command.
	 *
	 */
	pid_t child_pid = fork();
	if( child_pid == -1 ) {
		perror("fork");
		return false;
	}
	if( child_pid != 0 ) {
		// this is the parent:
		int status;
		do {
			int wait_ret = waitpid(child_pid, &status, WUNTRACED );
			if (wait_ret == -1) {
				perror("waitpid");
				return false;
			}
			if( WIFEXITED(status) ) {
				if( WEXITSTATUS(status) != EXIT_SUCCESS ) {
					return false;
				}
			} else if( WIFSIGNALED(status) ) {
				return false;
			} else if( WIFSTOPPED(status) ) {
				return false;
			} else {    // Non-standard case -- may never happen
				printf("Unexpected status (0x%x)\n", status);
				return false;
			}
		} while (!WIFEXITED(status) && !WIFSIGNALED(status));
	}
	else {
		// this is the child:
		if( -1 == execv( command[0], &command[0] ) ) {
			// perror("execv");
			exit(EXIT_FAILURE);
		}
	}
	return true;
}

/**
 * @param outputfile - The full path to the file to write with command output.
 *   This file will be closed at completion of the function call.
 * All other parameters, see do_exec above
 */
bool do_exec_redirect(const char *outputfile, int count, ...)
{
	va_list args;
	va_start(args, count);
	char * command[count+1];
	int i;
	for(i=0; i<count; i++)
	{
		command[i] = va_arg(args, char *);
	}
	va_end(args);
	command[count] = NULL;

	/*
	 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
	 *   redirect standard out to a file specified by outputfile.
	 *   The rest of the behaviour is same as do_exec()
	 *
	 */

	pid_t child_pid = fork();
	if( child_pid == -1 ) {
		perror("fork");
		return false;
	}
	if( child_pid != 0 ) {
		// this is the parent:
		int status;
		do {
			int wait_ret = waitpid(child_pid, &status, WUNTRACED );
			if (wait_ret == -1) {
				perror("waitpid");
				return false;
			}
			if( WIFEXITED(status) ) {
				if( WEXITSTATUS(status) != EXIT_SUCCESS ) {
					return false;
				}
				return true;
			} else if( WIFSIGNALED(status) ) {
				return false;
			} else if( WIFSTOPPED(status) ) {
				return false;
			} else {    // Non-standard case -- may never happen
				printf("Unexpected status (0x%x)\n", status);
				return false;
			}
		} while (!WIFEXITED(status) && !WIFSIGNALED(status));
	}
	else {
		// this is the child:
		int ret = EXIT_SUCCESS;
		int output_fd = open( outputfile, O_WRONLY | O_CREAT | O_TRUNC, 0x660 );
		if( -1 == output_fd ) {
			perror(outputfile);
			exit(EXIT_FAILURE);
		}
		// redirect stdout => output_fd
		dup2( output_fd, 1);
		if( -1 == execv( command[0], &command[0] ) ) {
			perror("execv");
			ret = EXIT_FAILURE;
		}
		if( -1 == close( output_fd ) ) {
			perror( outputfile );
			ret = EXIT_FAILURE;
		}
		exit(ret);
	}
	return true;
}
