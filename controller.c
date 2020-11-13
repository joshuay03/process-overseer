#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

#define FILE_ONLY 0
#define OUT_ONLY 1
#define LOG_ONLY 2
#define TIME_ONLY 3
#define OUT_AND_LOG 4
#define LOG_AND_TIME 5
#define OUT_AND_TIME 6
#define OUT_AND_LOG_AND_TIME 7

#define MAX_NUMARGS 20
#define BUF_LEN 256

char buf[BUF_LEN] = {0};

void sendNumArguments(int sockfd, int numargs)
{
    uint16_t statistics;
    statistics = htons(numargs);
    send(sockfd, &statistics, sizeof(uint16_t), 0);
}

void sendFlag(int sockfd, int flag)
{
    uint16_t statistics;
    statistics = htons(flag);
    send(sockfd, &statistics, sizeof(uint16_t), 0);
}


void sendArguments(int sockfd, int numargs, char *args[])
{
    int len;
    uint32_t netLen;
    char *msg;
    for (int i = 0; i < numargs; i++) {
        msg = args[i];
        len = strlen(msg);
        netLen = htonl(len);
        send(sockfd, &netLen, sizeof(netLen), 0);
        if (send(sockfd, msg, len, 0) != len) {
            fprintf(stderr, "send did not send all data\n");
            exit(1);
        }
    }
}

int main(int argc, char *argv[]) {
    int numargs;
    int flag;
    char *args[MAX_NUMARGS];

    if (argc == 2) {
        if (strcmp(argv[1], "--help") == 0) {
            fprintf(stderr, "Usage: controller <address> <port> {[-o out_file] [-log log_file] [-t seconds] <file> [arg...] | mem [pid] | memkill <percent>}\n");
        }
        else {
            fprintf(stderr, "Usage: controller <address> <port> {[-o out_file] [-log log_file] [-t seconds] <file> [arg...] | mem [pid] | memkill <percent>}\n");
        }

        exit(1);
    }
    else if (argc == 4) {
        numargs = 1;
        flag = FILE_ONLY;
        args[0] = argv[3];
    }
    else if (argc > 4) {
        if (argv[3][0] != '-') {
            numargs = argc - 3;
            flag = FILE_ONLY;
            for (int i = 3; i < argc; i++) {
                args[i - 3] = argv[i];
            }
        }
        else {
            for (int i = 3; i < argc; i = i+2) {
                if (argv[i][0] == '-') {
                    switch (argv[i][1]) {
                        case 'o':
                            if (i == 3) {
                                flag = OUT_ONLY;
                                args[0] = argv[4];
                            }
                            else {
                                fprintf(stderr, "Usage: controller <address> <port> {[-o out_file] [-log log_file] [-t seconds] <file> [arg...] | mem [pid] | memkill <percent>}\n");
                                exit(1);
                            }
                            break;
                        case 'l':
                            if (i == 3) {
                                flag = LOG_ONLY;
                                args[0] = argv[4];
                            }
                            else if (i == 5) {
                                flag = OUT_AND_LOG;
                                args[1] = argv[6];
                            }
                            else {
                                fprintf(stderr, "Usage: controller <address> <port> {[-o out_file] [-log log_file] [-t seconds] <file> [arg...] | mem [pid] | memkill <percent>}\n");
                                exit(1);
                            }
                            break;
                        case 't':
                            if (i == 3) {
                                flag = TIME_ONLY;
                                args[0] = argv[4];
                            }
                            else if (i == 5) {
                                if (flag == LOG_ONLY) {
                                    flag = LOG_AND_TIME;
                                }
                                else if (flag == OUT_ONLY) {
                                    flag = OUT_AND_TIME;
                                }
                                args[1] = argv[6];
                            }
                            else if (i == 7) {
                                flag = OUT_AND_LOG_AND_TIME;
                                args[2] = argv[8];
                            }
                            else {
                                fprintf(stderr, "Usage: controller <address> <port> {[-o out_file] [-log log_file] [-t seconds] <file> [arg...] | mem [pid] | memkill <percent>}\n");
                                exit(1);
                            }
                            break;
                    }
                }
                else {
                    int i;
                    int start;
                    switch (flag) {
                    case 1:
                    case 2:
                    case 3:
                        start = 1;
                        i = 5;
                        break;
                    case 4:
                    case 5:
                    case 6:
                        start = 2;
                        i = 7;
                        break;
                    case 7:
                        start = 3;
                        i = 9;
                        break;
                    }
                    while (i < argc) {
                        args[start] = argv[i];
                        start += 1;
                        i += 1;
                    }
                    numargs = start;
                }
            }
        }
    }

    int port, sockfd, numbytes;

    port = atoi(argv[2]);

    struct hostent *he;
    struct sockaddr_in their_addr; /* connector's address information */
    
    /* get the host info */
    if ((he = gethostbyname(argv[1])) == NULL) {
        herror("gethostbyname");
        exit(1);
    }

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    /* clear address struct */
    memset(&their_addr, 0, sizeof(their_addr));

    their_addr.sin_family = AF_INET;   /* host byte order */
    their_addr.sin_port = htons(port); /* short, network byte order */
    their_addr.sin_addr = *((struct in_addr *)he->h_addr);

    if (connect(sockfd, (struct sockaddr *)&their_addr, sizeof(struct sockaddr)) == -1) {
        fprintf(stderr, "Could not connect to overseer at %s %d\n", he->h_name, port);
        exit(1);
    }

    sendNumArguments(sockfd, numargs);
    sendFlag(sockfd, flag);
    sendArguments(sockfd, numargs, args);

    close(sockfd);
    
    return 0;
}