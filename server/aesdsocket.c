/************************************************************************************************************
Assignment Task: Create a socket based program with name aesdsocket in the “server” directory 

Assignemnt subtasks:
 - a. Is compiled by the “all” and “default” target of a Makefile in the “server” directory and 
        supports cross compilation, placing the executable file in the “server” directory and named
        aesdsocket.
 - b. Opens a stream socket bound to port 9000, failing and returning -1 if any of the socket 
        connection steps fail.
 - c. Listens for and accepts a connection
 - d. Logs message to the syslog “Accepted connection from xxx” where XXXX is the IP address of the 
        connected client.
 - e. Receives data over the connection and appends to file /var/tmp/aesdsocketdata, creating this 
        file if it doesn’t exist.
    - Your implementation should use a newline to separate data packets received. In other words a 
        packet is considered complete when a newline character is found in the input receive stream, 
        and each newline should result in an append to the /var/tmp/aesdsocketdata file.
    - You may assume the data stream does not include null characters (therefore can be processed using
        string handling functions).
    - You may assume the length of the packet will be shorter than the available heap size. In other 
        words, as long as you handle malloc() associated failures with error messages you may discard
         associated over-length packets.
- f. Returns the full content of /var/tmp/aesdsocketdata to the client as soon as the received data 
        packet completes.
    - You may assume the total size of all packets sent (and therefore size of /var/tmp/aesdsocketdata)
        will be less than the size of the root filesystem, however you may **not** assume this total 
        size of all packets sent will be less than the size of the available RAM for the process heap.
- g. Logs message to the syslog “Closed connection from XXX” where XXX is the IP address of the connected
        client.
- h. Restarts accepting connections from new clients forever in a loop until SIGINT or SIGTERM is received.
- i. Gracefully exits when SIGINT or SIGTERM is received, completing any open connection operations, 
        closing any open sockets, and **deleting the file /var/tmp/aesdsocketdata**.
    - Logs message to the syslog “Caught signal, exiting” when SIGINT or SIGTERM is received.
************************************************************************************************************/

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

#define PORT "9000"
#define BACKLOG 10
#define MAX_BUFFERS 16
#define MAXPACKETSIZE 65535 /* Theoretical maximum size for TCP segment - window size of 16 bit */

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
        exit(-1);
    }

    memset(local_ptr, 0, len);

    return local_ptr;
}

/** Receives data from the client and writes it to a file
 * 
 *  The buffer for the receiving data is allocated on the heap
 *  with the size of MAXPACKETSIZE
 */
void recv_data_from_client(int client_fd, int tmp_fd)
{
    char * rcv_buf = NULL;
    char * newline_ch = NULL;
    int bytes_rcv = 0;
    int total_bytes_pkt = 0;
    int bytes_written = 0; 
    
    rcv_buf = (char*)alloc_buffer(MAXPACKETSIZE);

    do { // A packet is considered complete when a newline is found
        bytes_rcv = recv(client_fd, &rcv_buf[total_bytes_pkt], MAXPACKETSIZE-total_bytes_pkt, 0);
        
        if (bytes_rcv == -1)
        {
            perror("recv");
            exit(-1);
        }
        else if (bytes_rcv == 0) // client closed the connection
        {
            fprintf(stderr, "Client closed connection!\n");
            break;
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

    printf("client (%d Bytes): %s", total_bytes_pkt, rcv_buf);

    // Write packet to tmp file
    bytes_written = write(tmp_fd, rcv_buf, total_bytes_pkt);
    if (bytes_written == -1)
    {
        perror("write");
        exit(-1);
    }
    else if (bytes_written != total_bytes_pkt)
    {
        fprintf(stderr, "write: not all bytes from buffer could be written to file - Information may be lost\n");
    }

    lseek(tmp_fd, 0, SEEK_SET); // Reset file position
    free(rcv_buf);
}

/** Reads data from file and sends it to the client
 * 
 *  The data will be read and sent by lines (i.e. until a newline character is found)
 *  or until the capacity of the buffer is reached; in that case a new buffer is allocated
 */
void send_tmp_data_to_client(int client_fd, int tmp_fd)
{
    char * newline_ch = NULL;
    char * send_buf = NULL;
    char * cur_buf = NULL;
    int bytes_read = 0;
    int bytes_sent = 0;
    int read_buf_len = 0;
    int cur_buf_len = 0;
    int buffers_used = 0;
    int bytes_read_total = 0;
    char* buffer_pool[MAX_BUFFERS] = { NULL }; // 16 x 4096 = 65536 (should be enough)

    // Use page size for the size of read buffer, usually 4096
    read_buf_len = getpagesize();

    for (int i = 0; i < MAX_BUFFERS && !newline_ch; i++)
    {
        buffer_pool[i] = (char*)alloc_buffer(read_buf_len);

        cur_buf = buffer_pool[i];
        cur_buf_len = read_buf_len;

        do {
            bytes_read = read(tmp_fd, cur_buf, cur_buf_len);

            // printf("Bytes read: %d\n", bytes_read);
            // printf("cur_buf_len: %d\n", cur_buf_len);

            if (bytes_read == -1)
            {
                perror("read");
                exit(-1);
            }
    
            newline_ch = strchr(cur_buf, '\n');
            cur_buf_len -= bytes_read;
            cur_buf += bytes_read;
            bytes_read_total += bytes_read;

            if (newline_ch) { break; } // Line found

        } while (cur_buf_len != 0 && bytes_read != 0 && newline_ch == NULL);

        if (newline_ch == NULL && cur_buf_len == 0) // no newline found and buffer is full
        {
            printf("No new line found! - allocating a new buffer!\n");
        }
        
        buffers_used = i+1;
    }

    // printf("Bytes read total (final): %d\n", bytes_read_total);
    // printf("Buffers used: %d\n", buffers_used);

    if (!newline_ch && buffers_used == MAX_BUFFERS)
    {
        printf("No new line found and no more buffers available! - string may be incomplete!\n");
    }

    send_buf = (char*)alloc_buffer(buffers_used * read_buf_len);

    // Copy content of file in send buffer
    for (int i = 0; i < buffers_used; i++)
    {
        memcpy(send_buf + i*read_buf_len, buffer_pool[i], read_buf_len);
    }

    // printf("Content of the file/buffer before send:\n%s", send_buf);

    bytes_sent = send(client_fd, send_buf, bytes_read_total, 0);

    if (bytes_sent == -1 ) { perror("send"); }

    // printf("Bytes sent: %d\n", bytes_sent);

    // Free buffers
    free(send_buf);
    for (int i = 0; i < buffers_used; i++)
    {
        free(buffer_pool[i]);
    }
}

void sigexit_handler(int signal_number)
{
    if(signal_number == SIGINT)
    {
        printf("Caught SIGINT signal, exiting\n");
        syslog(LOG_INFO, "Caught signal, exiting");

        exit(0);


    }
    else if (signal_number == SIGTERM)
    {
        printf("Caught SIGTERM signal, exiting\n");
        syslog(LOG_INFO, "Caught signal, exiting");

        exit(0);
    }
}

int main(void)
{
    int sockfd, client_fd;  // listen on sock_fd, new connection on client_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction action_children, action_exit;
    int yes=1;
    char ipaddr_client[INET6_ADDRSTRLEN];
    int rv;

    // Set up syslog
    openlog(NULL, 0, LOG_USER);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("setsockopt");
            exit(-1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(-1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(-1);
    }

    // Setup signal handling
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

    printf("server: waiting for connections...\n");

    while(1) {  // main accept() loop
        sin_size = sizeof their_addr;
        client_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (client_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr *)&their_addr),
                  ipaddr_client, sizeof ipaddr_client);
        printf("server: got connection from %s\n", ipaddr_client);
        syslog(LOG_INFO, "Accepted connection from %s", ipaddr_client);

        if (!fork())
        { // this is the child process
            close(sockfd); // child doesn't need the listener
            int tmp_fd;

            if (send(client_fd, "Connection with the server established.\n", 41, 0) == -1) {
                perror("send");
            }
            
            // Open file
            const char *filename = "/var/tmp/aesdsocketdata";
            tmp_fd = open(filename, O_RDWR|O_CREAT|O_APPEND, S_IRWXU|S_IRWXG|S_IRWXO);

            if (tmp_fd == -1)
            {
                perror("open");
                exit(-1);
            }
            
            recv_data_from_client(client_fd, tmp_fd);

            send_tmp_data_to_client(client_fd, tmp_fd);

            close(client_fd);
            close(tmp_fd);
            syslog(LOG_INFO, "Closed connection from %s", ipaddr_client);

            exit(0);
        }
        close(client_fd);  // parent doesn't need this
    }

    return 0;
}