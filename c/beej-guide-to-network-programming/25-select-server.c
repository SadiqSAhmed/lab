#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#define arrlen(a) (sizeof (a) / sizeof (*(a)))

void print_sockaddr(const char *msg, struct sockaddr *sa)
{
    char ip[INET6_ADDRSTRLEN];

    printf("%s", msg);

    if (sa->sa_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *) sa;

        printf("ipv4: %s; port: %d\n",
               inet_ntop(AF_INET, &sin->sin_addr, ip, arrlen(ip)),
               ntohs(sin->sin_port));

    } else if (sa->sa_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) sa;

        printf("ipv6: %s; port: %d\n",
               inet_ntop(AF_INET6, &sin6->sin6_addr, ip, arrlen(ip)),
               ntohs(sin6->sin6_port));
    } else {
        printf("Unsupported address family: %d\n", sa->sa_family);
    }
}

void print_addrinfo(const char *msg, struct addrinfo *ai)
{
    printf("%s", msg);
    printf("family: %d; socktype: %d; protocol: %d; ",
           ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    print_sockaddr("", ai->ai_addr);
}

int main()
{
    fd_set master;
    fd_set fds;
    int fdmax;

    int sockfd, connfd;
    int ret;
    int yes = 1;
    int run = 1;

    struct addrinfo hints, *ai, *aii;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((ret = getaddrinfo(NULL, "9090", &hints, &ai)) == -1) {
        fprintf(stderr, "S: getaddrinfo() error: %s\n", gai_strerror(ret));
        return EXIT_FAILURE;
    }

    for (aii = ai; aii != NULL; aii = aii->ai_next) {
        print_addrinfo("S: Found address: ", aii);

        sockfd = socket(aii->ai_family, aii->ai_socktype, aii->ai_protocol);
        if (sockfd == -1) {
            perror("S: socket() error");
            continue;
        }

        ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        if (ret == -1) {
            perror("S: setsockopt() error");
            close(sockfd);
            return EXIT_FAILURE;
        }

        if (bind(sockfd, aii->ai_addr, aii->ai_addrlen) == -1) {
            perror("S: bind() error");
            close(sockfd);
            continue;
        }

        break;
    }

    freeaddrinfo(ai);

    if (aii == NULL) {
        fprintf(stderr, "S: error: could not bind to any address\n");
        return EXIT_FAILURE;
    }

    if (listen(sockfd, 2) == -1) {
        perror("S: listen() error");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("S: Listening ...\n");

    FD_ZERO(&master);
    FD_ZERO(&fds); // TODO necessary?

    FD_SET(sockfd, &master);
    fdmax = sockfd;

    // accept() loop
    while (run) {
        fds = master;

        printf("S: Waiting ...\n");
        if (select(fdmax + 1, &fds, NULL, NULL, NULL) == -1) {
            perror("S: select() error");
            return EXIT_FAILURE;
        }

        for (int fd = 0; fd <= fdmax; fd++) {
            if (FD_ISSET(fd, &fds)) {
                if (fd == sockfd) {
                    struct sockaddr_storage conn_addr;
                    socklen_t conn_addr_len = sizeof conn_addr;
                    connfd = accept(sockfd, (struct sockaddr *) &conn_addr, &conn_addr_len);
                    if (connfd == -1) {
                        perror("S: accept() error");
                        continue;
                    }

                    print_sockaddr("S: Accepted: ", (struct sockaddr *) &conn_addr);
                    
                    FD_SET(connfd, &master);
                    if (connfd > fdmax)
                        fdmax = connfd;
                } else {
                    char buffer[1024];
                    ssize_t bytes;

                    bytes = recv(fd, buffer, arrlen(buffer), 0);
                    if (bytes <= 0) {
                        if (bytes == -1) {
                            perror("S: recv() error");
                        } else if (bytes == 0) {
                            printf("S: recv(): connection closed\n");
                        }
                        close(fd);
                        FD_CLR(fd, &master);
                        continue;
                    }

                    printf("S: recv() %jd bytes: %.*s",
                           (intmax_t) bytes, (int) bytes, buffer);

                    if (strncmp(buffer, "exit", 4) == 0)
                        run = 0;

                    strcpy(buffer, "hello from server\n");
                    bytes = send(fd, buffer, strlen(buffer), 0);
                    if (bytes == -1) {
                        perror("S: send() error");
                        close(fd);
                        continue;
                    }
                    printf("S: send() %jd bytes: %s", strlen(buffer), buffer);
                } // end if-statement for fd == sockfd
            } // end if-statement for FD_ISSET.
        } // end for-loop for iterating over each fd.
    } // end while select() loop

    printf("Done!\n");
}
