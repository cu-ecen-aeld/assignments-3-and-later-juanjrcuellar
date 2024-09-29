#include "systemcalls.h"
// #define _XOPEN_SOURCE
#define __USE_XOPEN
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <linux/limits.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{
    int ret;

    ret = system(cmd);

    if (WIFEXITED(ret))
    {
        printf("Normal termination with exit status=%d\n", WEXITSTATUS(ret));

        if (WEXITSTATUS(ret) == EXIT_SUCCESS)
            return true;
        else
            return false;
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
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
        printf("Command: %s\n", command[i]);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];

/* Assignment implementation */

    char program[NAME_MAX];

    printf("\tCommand: %s\n", command[0]);
    printf("\tCount: %d\n", count);

    // By convention, the first argument is the name of the program
    // If any slash is found, we can assume is not an absolute path
    char * found = strrchr(command[0], '/');
    
    if (found == NULL)
    {
        printf("Error: the command has to be given as an absolute path\n");
        return false;
    }
    else
    {
        printf("strrchr(): %s\n", found);
        strncpy(program, found+1, NAME_MAX);
        printf("program: %s\n", program);
    }

    // Remember: Command is an array of pointers to individual strings, not an whole char array (string)

    int ret, status;
    pid_t pid;
    char * argv[count];

    argv[0] = program;
    printf("argv[0]: %s\n", argv[0]);

    for(int j = 1; j != count; j++)
    {
        argv[j] = command[j];
        printf("argv[%d]: %s\n", j, argv[j]);
    }
    argv[count] = NULL;

    pid = fork();

    if(pid == -1)
    {
        perror("fork");
        return false;
    }
    else if(pid == 0)
    {
        printf("I am the child: %d\n", pid);
        ret = execv(command[0], argv);
        printf("This means that there was an error - ret: %d\n", ret);

        if (ret == -1)
        {
            perror("execv");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        printf("I am the parent: %d\n", pid);

        if (wait (&status) == -1)
        {
            perror("wait");
            return false;
        }
        else if (WIFEXITED(status))
        {
            va_end(args);
            printf("Exited with status %d - succeed\n", WEXITSTATUS(status));
            return WIFEXITED(status) && (WEXITSTATUS(status) == 0);
            //return true;
        }
    }
    // TODO: is really everything ok?

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
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];


/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/

    va_end(args);

    return true;
}
