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
#define MAXBUFFERSIZE 10
#define MAXTCPSEGMENTSIZE 65535

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

int main(void)
{
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char ipaddr_client[INET6_ADDRSTRLEN];
    char rcv_buf[MAXBUFFERSIZE+1];
    char packet_buf[MAXTCPSEGMENTSIZE];
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

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(-1);
    }

    printf("server: waiting for connections...\n");

    while(1) {  // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr *)&their_addr),
                  ipaddr_client, sizeof ipaddr_client);
        printf("server: got connection from %s\n", ipaddr_client);
        syslog(LOG_DEBUG, "Accepted connection from %s", ipaddr_client);

        if (!fork()) { // this is the child process
            close(sockfd); // child doesn't need the listener
            int fd;

            if (send(new_fd, "Connection with the server established.\n", 41, 0) == -1) {
                perror("send");
            }
            
            // Open file
            const char *filename = "/var/tmp/aesdsocketdata";
            fd = open(filename, O_WRONLY|O_CREAT|O_APPEND, S_IRWXU|S_IRWXG|S_IRWXO);

            if (fd == -1)
            {
                perror("open");
                // fprintf(stderr, "Error opening %s\n", filename);
                // syslog(LOG_ERR, "Error opening %s\n", filename);
                return -1;
            }
            
            char * newline_ch = NULL;
            int bytes_rcv = 0;
            int bytes_pkt = 0;
            memset(packet_buf, 0, MAXTCPSEGMENTSIZE);
                        
            do { // A packet is considered complete when a newline is found
                memset(rcv_buf, 0, MAXBUFFERSIZE+1);
                if ((bytes_rcv = recv(new_fd, rcv_buf, MAXBUFFERSIZE, 0)) == -1) perror("recv");

                // The data stream does not include null characters
                // String handling functions like strcat require null-terminated strings
                rcv_buf[bytes_rcv+1] = '\0';
                printf("  packet: %s\n", rcv_buf);
                printf("  packet: number of bytes received: %d\n", bytes_rcv);

                bytes_pkt += bytes_rcv;
                strcat(packet_buf, rcv_buf);

                newline_ch = strchr(rcv_buf, '\n');
                if (newline_ch == NULL) printf("EoL not found\n");
            } while (newline_ch == NULL);

            printf("EoL found\n");

            printf("server: total received: %s\n", packet_buf);
            printf("server: total number of bytes received: %d\n", bytes_pkt);

            write(fd, packet_buf, bytes_pkt);

            close(new_fd);
            close(fd);
            exit(0);
        }
        close(new_fd);  // parent doesn't need this
    }

    return 0;
}