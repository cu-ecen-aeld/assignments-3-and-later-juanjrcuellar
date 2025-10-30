/******************************************************************************
Assignment 6 part 1: Native Socket Server Threading Support

New implementation details according to assignment instructions:

 * Theading and Linked Lists *

    - The program accepts now multiple simultaneous connections, with each 
    connection spawning a new thread.

    - Writes to /var/tmp/aesdsocketdata should be synchronized between threads 
    using a mutex, to ensure data written by synchronous connections is not 
    intermixed.

    - A thread exits when the connection is closed by the client or when an 
    error occurs in the send or receive steps.

    - The program continues to gracefully exit when SIGTERM/SIGINT is received,
    after requesting an exit from each thread and waiting for threads to 
    complete execution.

    - A singly linked list is used to managed threads, specifically the API from 
    the revised BSD's queue.h implementation from https://github.com/stockrt/queue.h

    - To join completed threads pthread_join() is used.

 * Timestamp *

    - A timestamp in the form “timestamp:time” is appended to the /var/tmp/
    aesdsocketdata file every 10 seconds. The time is specified by the RFC 2822
    compliant strftime format, followed by newline. The string includes the 
    year, month, day, hour (in 24 hour format) minute and second representing 
    the system wall clock time.

    - A locking mechanism is used to ensure the timestamp is written 
    atomically with respect to socket data.

******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <pthread.h>
#include "queue.h"
#include <time.h>

#define PORT "9000"
#define BACKLOG 10
#define MAX_BUFFERS 16
#define MAXPACKETSIZE 65535 /* Theoretical maximum size for TCP segment - window size of 16 bit */
#define TMPFILEPATH "/var/tmp/aesdsocketdata"

/************
 Declarations
************/

struct slist_data_s
{
    pthread_t pthread_id;
    int client_fd;
    char ipaddr_client[INET6_ADDRSTRLEN];
    bool finished;
    SLIST_ENTRY(slist_data_s) entries;
};
typedef struct slist_data_s slist_data_t;

struct tmpfile_s
{
    int fd;
    pthread_mutex_t lock;
};
typedef struct tmpfile_s tmpfile_t;

/****************
 Global variables
 ***************/

tmpfile_t g_tmpfile = {-1, PTHREAD_MUTEX_INITIALIZER};
int sock_fd = -1;

volatile int signal_caught = 0;

/*********
 Functions
 ********/

void sigchld_handler(int s)
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/** Allocates memory for a buffer and returns its address
 * 
 *  It allocates len bytes on the heap using malloc and initializes them to 0. 
 */
void* alloc_buffer(int len)
{
    void * local_ptr = malloc(len);

    if (!local_ptr)
    {
        fprintf(stderr, "malloc for allocating buffer failed\n");
        pthread_exit(NULL);
    }

    memset(local_ptr, 0, len);

    return local_ptr;
}

/** Receives data from the client and writes it to a file
 * 
 *  The buffer for the receiving data is allocated on the heap
 *  with the size of MAXPACKETSIZE
 */
void recv_data_from_client(int client_fd)
{
    char * rcv_buf = NULL;
    char * newline_ch = NULL;
    int bytes_rcv = 0;
    int total_bytes_pkt = 0;
    int bytes_written = 0;

    // Check if needed fds were open
    if (client_fd == -1 || g_tmpfile.fd == -1)
    {
        fprintf(stderr, "Error: client connection or temp file were not open!");
        pthread_exit(NULL);
    }
    
    rcv_buf = (char*)alloc_buffer(MAXPACKETSIZE);

    do { // A packet is considered complete when a newline is found
        bytes_rcv = recv(client_fd, &rcv_buf[total_bytes_pkt], MAXPACKETSIZE-total_bytes_pkt, 0);
        if (bytes_rcv == -1)
        {
            perror("recv");
            free(rcv_buf);
            pthread_exit(NULL);
        }
        else if (bytes_rcv == 0) // client closed the connection - exit thread
        {
            fprintf(stderr, "Error: Client closed connection before a new line was found!\n");
            free(rcv_buf);
            pthread_exit(NULL);
        }

        total_bytes_pkt += bytes_rcv;

        if (total_bytes_pkt == MAXPACKETSIZE) break;

        newline_ch = strchr(rcv_buf, '\n');

    } while (!newline_ch);

    if (total_bytes_pkt == MAXPACKETSIZE)
    {
        fprintf(stderr, "Oversized packet - Limit of MAXPACKETSIZE (65535) exceeded - Information may be lost\n");
        
        if (rcv_buf[MAXPACKETSIZE-1] != '\n')
        {
            // Truncate the string adding a newline character at the end
            rcv_buf[MAXPACKETSIZE-1] = '\n';
        }
    }

    // Write packet to tmp file - Critical region
    pthread_mutex_lock(&g_tmpfile.lock);
    lseek(g_tmpfile.fd, 0, SEEK_END); // Set position at the end, so that the new content will be appended
    bytes_written = write(g_tmpfile.fd, rcv_buf, total_bytes_pkt);
    pthread_mutex_unlock(&g_tmpfile.lock);

    if (bytes_written == -1)
    {
        perror("write");
        free(rcv_buf);
        pthread_exit(NULL);;
    }
    else if (bytes_written != total_bytes_pkt)
    {
        fprintf(stderr, "write: not all bytes from buffer could be written to file - Information may be lost\n");
    }

    free(rcv_buf);
}

/** Reads data from file and sends it to the client
 * 
 *  The data will be read into a buffer with the size of the page size.
 *  When the buffer is full, a new buffer from a buffer pool will be allocated.
 *  When EOF is reached, a buffer for send will be allocated with the size of the buffers used to read.
 */
void send_tmp_data_to_client(int client_fd)
{
    char * send_buf = NULL;
    char * cur_buf = NULL;
    int bytes_read = 0;
    int bytes_sent = 0;
    int read_buf_len = 0;
    int cur_buf_len = 0;
    int buffers_used = 0;
    int bytes_read_total = 0;
    char* buffer_pool[MAX_BUFFERS] = { NULL }; // 16 x 4096 = 65536 (should be enough)

    // Check if needed fds were open
    if (client_fd == -1 || g_tmpfile.fd == -1)
    {
        fprintf(stderr, "Error: client connection or temp file were not open!");
        pthread_exit(NULL);
    }

    // Use page size for the size of read buffer, usually 4096
    read_buf_len = getpagesize();

    pthread_mutex_lock(&g_tmpfile.lock); // read from tmpfile - critical region
    lseek(g_tmpfile.fd, 0, SEEK_SET); // Reset file position before read

    for (int i = 0; i < MAX_BUFFERS; i++)
    {
        buffer_pool[i] = (char*)alloc_buffer(read_buf_len);

        cur_buf = buffer_pool[i];
        cur_buf_len = read_buf_len;

        do {
            bytes_read = read(g_tmpfile.fd, cur_buf, cur_buf_len);
            if (bytes_read == -1)
            {
                perror("read");
                for (int j = 0; j <= i; j++)
                {
                    free(buffer_pool[j]);
                }
                pthread_exit(NULL);
            }
    
            cur_buf_len -= bytes_read;
            cur_buf += bytes_read;
            bytes_read_total += bytes_read;

        } while (cur_buf_len != 0 && bytes_read != 0);
        
        buffers_used = i+1;

        if (bytes_read == 0) {break;}
    }
    pthread_mutex_unlock(&g_tmpfile.lock);

    if (buffers_used == MAX_BUFFERS)
    {
        printf("No new line found and no more buffers available! - string may be incomplete!\n");
    }

    send_buf = (char*)alloc_buffer(buffers_used * read_buf_len);

    // Copy content of file in send buffer
    for (int i = 0; i < buffers_used; i++)
    {
        memcpy(send_buf + i*read_buf_len, buffer_pool[i], read_buf_len);
    }

    bytes_sent = send(client_fd, send_buf, bytes_read_total, 0);

    if (bytes_sent == -1 ) { perror("send"); }

    // Free buffers
    free(send_buf);
    for (int i = 0; i < buffers_used; i++)
    {
        free(buffer_pool[i]);
    }
}

void * connection_thread (void *arg)
{
    slist_data_t *node = arg;
    
    recv_data_from_client(node->client_fd);

    send_tmp_data_to_client(node->client_fd);

    node->finished = true;

    return arg;
}

void sigexit_handler(int signal_number)
{
    if(signal_number == SIGINT || signal_number == SIGTERM)
    {
        signal_caught = 1;
    }
}

int setup_connection()
{
    struct addrinfo hints, *servinfo, *p;
    int yes = 1;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
        {
            perror("setsockopt");
            exit(-1);
        }

        if (bind(sock_fd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sock_fd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)
    {
        fprintf(stderr, "server: failed to bind\n");
        exit(-1);
    }

    return sock_fd;
}

void setup_signal_handling()
{
    struct sigaction action_children, action_exit;

    memset(&action_children, 0, sizeof(struct sigaction));
    action_children.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&action_children.sa_mask);
    action_children.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &action_children, NULL) == -1) {
        perror("sigaction");
        exit(-1);
    }

    memset(&action_exit, 0, sizeof(struct sigaction));
    action_exit.sa_handler = sigexit_handler;
    if (sigaction(SIGTERM, &action_exit, NULL) != 0)
    {
        fprintf(stderr, "Error %d (%s) registering for SIGTERM", errno, strerror(errno));
    }
    if (sigaction(SIGINT, &action_exit, NULL) != 0)
    {
        fprintf(stderr, "Error %d (%s) registering for SIGINT", errno, strerror(errno));
    }

}

static void timer_thread(union sigval sigval)
{
    int ret;
    struct tm current_time;
    char tmstamp_string[100];
    char tmstamp_out[200] = "timestamp:";
    struct timespec ts_value;

    // Get current time
    ret = clock_gettime(CLOCK_REALTIME, &ts_value);
    if( ret != 0 )
    {
        fprintf(stderr, "Error %d (%s) getting clock CLOCK_REALTIME\n",
                errno, strerror(errno));
    }

    // Convert time_t to tm
    if (gmtime_r(&ts_value.tv_sec, &current_time) == NULL )
    {
        fprintf(stderr, "Error calling gmtime_r with time %ld\n", ts_value.tv_sec);
    }

    // RFC 822 compliant date format
    if ( strftime(tmstamp_string, sizeof(tmstamp_string), "%a, %d %b %y %T", &current_time) == 0 )
    {
        fprintf(stderr, "Error converting string with strftime\n");
    }

    strcat(tmstamp_out, tmstamp_string);
    strcat(tmstamp_out, "\n");

    // Write timestamp to tmp file - Critical region
    pthread_mutex_lock(&g_tmpfile.lock);
    lseek(g_tmpfile.fd, 0, SEEK_END); // Set position at the end, so that the new content will be appended
    ret = write(g_tmpfile.fd, tmstamp_out, strlen(tmstamp_out) + 1);
    pthread_mutex_unlock(&g_tmpfile.lock);

    if (ret == -1)
    {
        perror("write");
    }
}

timer_t setup_timer_timestamping()
{
    struct sigevent sev;
    memset(&sev, 0, sizeof(struct sigevent));
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = timer_thread;
    timer_t timer_id;

    if (timer_create(CLOCK_MONOTONIC, &sev, &timer_id) != 0)
    {
        perror("timer_create");
        exit(-1);
    }

    struct itimerspec its;
    memset(&its, 0, sizeof(struct itimerspec));
    its.it_interval.tv_sec = 10;
    its.it_value.tv_sec = 10;


    if(timer_settime(timer_id, 0, &its, NULL) != 0)
    {
        perror("timer_settime");
        exit(-1);
    }

    return timer_id; // needed to delete the timer
}

int main(int argc, char* argv[])
{
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    char ipaddr_client[INET6_ADDRSTRLEN];
    bool daemon_mode = false;
    pid_t pid;

    if (argc > 2)
    {
        printf("Error: too many arguments\n");
        printf("Usage: %s [-d]\n", argv[0]);
        printf("\t-d: run in daemon mode\n");
        return -1;
    }
    else if (argc == 2)
    {
        if (strcmp("-d", argv[1]) != 0)
        {
            printf("Error: invalid option\n");
            printf("Usage: %s [-d]\n", argv[0]);
            printf("\t-d: run in daemon mode\n");
            return -1;
        }

        daemon_mode = true;
    }
    else
    {
        daemon_mode = false;
    }

    // Setup
    openlog(NULL, 0, LOG_USER);

    setup_signal_handling();

    sock_fd = setup_connection();

    if (daemon_mode)
    {
        pid = fork();
        if (pid == -1)
        {
            perror("fork");
            exit(-1);
        }
        else if (pid != 0) // Parent
        {
            close(sock_fd);
            exit(0);
        }

        if (setsid() == -1) return -1;
        if (chdir ("/") == -1) return -1;

        /* redirect fd's 0,1,2 to /dev/null */
        close(0);
        close(1);
        close(2);
        open ("/dev/null", O_RDWR); /* stdin (0) -> /dev/null */
        dup (0);                    /* stdout (1) -> 0 -> /dev/null */
        dup (0);                    /* stderror (2) -> 0 -> /dev/null */
    }

    if (listen(sock_fd, BACKLOG) == -1)
    {
        perror("listen");
        exit(-1);
    }

    // Open tmp file
    const char *filename = TMPFILEPATH;
    g_tmpfile.fd = open(filename, O_RDWR|O_CREAT|O_APPEND, S_IRWXU|S_IRWXG|S_IRWXO);

    if (g_tmpfile.fd == -1)
    {
        perror("open");
        exit(-1);
    }

    // Create timer for time stamping
    timer_t timer_id = setup_timer_timestamping();

    // Initialize Linked List
    slist_data_t *slist_node = NULL;
    SLIST_HEAD(slisthead, slist_data_s) head;
    SLIST_INIT(&head);

    while(!signal_caught) // main accept() loop
    {
        sin_size = sizeof their_addr;
        int new_client_fd = accept(sock_fd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_client_fd == -1)
        {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr *)&their_addr),
                  ipaddr_client, sizeof ipaddr_client);
        syslog(LOG_INFO, "Accepted connection from %s", ipaddr_client);

        slist_node = malloc(sizeof(slist_data_t));
        if (slist_node == NULL)
        { 
            close(new_client_fd);
            continue;
        }
        memset(slist_node, 0, sizeof(slist_data_t));
        slist_node->finished = false; // set here to avoid race conditions

        pthread_t new_thread;
        pthread_create (&new_thread, NULL, connection_thread, (void *)slist_node);
        
        slist_node->pthread_id = new_thread;
        slist_node->client_fd = new_client_fd;
        memcpy(slist_node->ipaddr_client, ipaddr_client, sizeof(ipaddr_client));
        
        SLIST_INSERT_HEAD(&head, slist_node, entries);

        slist_data_t *temp_node = NULL;
        SLIST_FOREACH_SAFE(slist_node, &head, entries, temp_node)
        {
            if (slist_node->finished)
            {
                pthread_join(slist_node->pthread_id, NULL);
                close(slist_node->client_fd);
                syslog(LOG_INFO, "Closed connection from %s", slist_node->ipaddr_client);

                SLIST_REMOVE(&head, slist_node, slist_data_s, entries);
                free(slist_node);
            }
        }
    }

    // Clean-up after signal was caught
    // It is garanteed to fall here because according to the man page of
    // signal() if a blocked call to accept (among others interfaces) is
    // interrupted by a signal handler, the call fails with EINTR.

    syslog(LOG_INFO, "Caught signal, exiting");

    close(sock_fd);

    // Request an exit from each thread
    while (!SLIST_EMPTY(&head))
    {
        slist_node = SLIST_FIRST(&head);
        pthread_cancel(slist_node->pthread_id);
        close(slist_node->client_fd);
        SLIST_REMOVE_HEAD(&head, entries);
        free(slist_node);
    }

    timer_delete(timer_id);

    // Close and delete tmp file
    close(g_tmpfile.fd);
    unlink(TMPFILEPATH);

    return 0;
}
