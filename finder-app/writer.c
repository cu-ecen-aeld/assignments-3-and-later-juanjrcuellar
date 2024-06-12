/**
 * Alternative application to writer.sh from assignment 1
 * Here we are using File IO directly
 * From writer.sh:
 *  - Accepts the following arguments:
 *      1. the first argument is a full path to a file (including filename) on 
 *         the filesystem
 *      2. the second argument is a text string which will be written within 
 *         this file
 *  - Exits with value 1 error and print statements if any of the arguments 
 *    above were not specified
 *  - Creates a new file with name and path writefile with content writestr, 
 *    overwriting any existing file and creating the path if it doesn’t exist. 
 *    Exits with value 1 and error print statement if the file could not be created.
 *  Assumptions: no need to create directories which do not exist. It is created
 *  by the caller.
*/

#include <stdio.h>
#include <fcntl.h> // for open
#include <unistd.h> // for read, write and close
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <error.h>
#include <string.h>
#include <syslog.h>

int main (int argc, char *argv[])
{
    int fd;

    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <file> <string>\n", argv[0]);
        printf("    The file specified as \"file\" will be created if it does not exist.\n");
        return 1;
    }

    // Set up syslog
    openlog(NULL, 0, LOG_USER);

    // Open file
    const char *filename = argv[1];
    fd = open(filename, O_WRONLY|O_CREAT, S_IRWXU|S_IRWXG|S_IRWXO);

    if (fd == -1)
    {
        printf("Error opening %s\n", filename);
        syslog(LOG_ERR, "Error opening %s\n", filename);
        return 1;
    }

    // Write string
    const char *writestr = argv[2];
    size_t count;
    ssize_t bytes_written;
    
    count = strlen(writestr);
    bytes_written = write(fd, writestr, count);
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, filename);

    if (bytes_written == -1)
    {
        printf("Error from write, errno was %d (%s)\n", errno, strerror(errno));
        syslog(LOG_ERR, "Error from write, errno is %d (%s)", errno, strerror(errno));
    }
    else if ( bytes_written != count)
    {
        printf("Unexpected short write of %zd (expected %ld bytes)\n", bytes_written, count);
        syslog(LOG_ERR, "Unexpected short write of %zd (expected %ld bytes)", bytes_written, count);
    }
    else
    {
        printf("String %s successfully written to %s\n", writestr, filename);
        syslog(LOG_DEBUG, "String %s successfully written to %s", writestr, filename);
    }

    // Close file
    close(fd);

    return 0;
}