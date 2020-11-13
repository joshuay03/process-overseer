#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <sys/resource.h>

#define FILE_ONLY 0
#define OUT_ONLY 1
#define LOG_ONLY 2
#define TIME_ONLY 3
#define OUT_AND_LOG 4
#define LOG_AND_TIME 5
#define OUT_AND_TIME 6
#define OUT_AND_LOG_AND_TIME 7

#define KEY ((key_t) 1234)
#define SIZE sizeof(int)
#define MAX_NUMARGS 20
#define NUM_THREADS 5 /* size of the thread pool */
#define BACKLOG 10 /* number of pending connections queue will hold */
#define BUFF_LEN 256 /* length of time buffer */

char timebuff[BUFF_LEN] = {0};

typedef struct node {
    int numargs;
    int flag;
    char *args[MAX_NUMARGS];
    struct node *next;
} node_t;

pthread_mutex_t mutex;
pthread_cond_t cond;

char* getCurrentTime() {
    time_t rawtime = time(NULL);
    struct tm *ptm = localtime(&rawtime);
    strftime(timebuff, BUFF_LEN, "%F %T", ptm);

    return timebuff;
}

int receiveNumArguments(int sockid) {
    int number_of_bytes;
    uint16_t statistics;
    
    if ((number_of_bytes = recv(sockid, &statistics, sizeof(uint16_t), 0)) == -1) {
        perror("recv");
        exit(EXIT_FAILURE);
    }
    int results = ntohs(statistics);
    
    return results;
}

int receiveFlag(int sockid) {
    int number_of_bytes;
    uint16_t statistics;
    
    if ((number_of_bytes = recv(sockid, &statistics, sizeof(uint16_t), 0)) == -1) {
        perror("recv");
        exit(EXIT_FAILURE);
    }
    int results = ntohs(statistics);
    
    return results;
}

char *receiveArguments(int sockid) {
    char *msg;
    uint32_t netLen;
    int recvLen = recv(sockid, &netLen, sizeof(netLen), 0);
    if (recvLen != sizeof(netLen)) {
        fprintf(stderr, "recv got invalid length value (got %d)\n", recvLen);
        exit(1);
    }
    int len = ntohl(netLen);
    msg = malloc(len + 1);
    if (recv(sockid, msg, len, 0) != len) {
        fprintf(stderr, "recv got invalid length msg\n");
        exit(1);
    }
    msg[len] = '\0';

    return msg;
}

void print_list(node_t * head) {
    node_t * current = head;
    
    while (current != NULL) {
        printf("node start\n");
        printf("%d\n", current->numargs);
        printf("%d\n", current->flag);
        for (int i = 0; i < current->numargs; i++) {
            printf("%s\n", current->args[i]);
        }
        printf("node end\n");
        current = current->next;
    }
}

void push(node_t *head, int fd) {
    node_t *new_node;
    new_node = (node_t *) malloc(sizeof(node_t));

    new_node->numargs = receiveNumArguments(fd);
    new_node->flag = receiveFlag(fd);
    for (int i = 0; i < new_node->numargs; i++) {
        new_node->args[i] = receiveArguments(fd);
    }
    new_node->next=head->next;
    head->next = new_node;
}

void remove_last(node_t *head) {
    int retnumargs = 0;
    char **retargs;
    /* if there is only one item in the list, remove it */
    if (head->next == NULL) {
        retnumargs = head->numargs;
        retargs = head->args;
        free(head);
    }

    /* get to the second to last node in the list */
    node_t *current = head;
    while (current->next->next != NULL) {
        current = current->next;
    }

    /* current points to the second to last item of the list, remove current->next */
    retnumargs = current->next->numargs;
    retargs = current->next->args;
    free(current->next);
    current->next = NULL;
}

node_t *get_last(node_t *head) {
    node_t *current = head;

    while (current->next->next != NULL) {
        current = current->next;
    }

    current = current->next; /* current points to the last item of the list */
    
    return current;
}

void *runner(void *head) {
    while(1){
        int rc = pthread_mutex_lock(&mutex);
        if (rc) {
            perror("pthread_mutex_lock");
            pthread_exit(NULL);
        }
        rc = pthread_cond_wait(&cond, &mutex);

        if (rc == 0) {
            pid_t pid;
            int childstatus;

            node_t *request = get_last(head);


            char *filepath;
            bool havefileargs = false;
            int numfileargs;
            char *fileargs[MAX_NUMARGS];

            switch (request->flag) {
            case 0:
                if (request->numargs > 1) {
                    havefileargs = true;
                    numfileargs = request->numargs - 1;
                    for (int i = 1; i < request->numargs; i++) {
                        fileargs[i - 1] = request->args[i];
                    }
                }
                filepath = request->args[0];
                break;
            case 1:
            case 2:
            case 3:
                filepath = request->args[1];
                break;
            case 4:
            case 5:
            case 6:
                filepath = request->args[2];
                break;
            case 7:
                filepath = request->args[3];
                break;
            }

            fprintf(stdout, "%s - attempting to execute %s\n", getCurrentTime(), filepath);

            int* resexec;
            int id = shmget(KEY, SIZE, IPC_CREAT | 0666);
            resexec = (int*) shmat(id, 0, 0);
            *resexec == 0;
            pid = fork();
            if (pid == 0) {
                if (request->flag == 1 | request->flag == 4 | request->flag == 6) {
                    int out_fd = fileno(fopen(request->args[0], "w"));
                    dup2(out_fd, 1);
                    dup2(out_fd, 2);
                    close(out_fd);
            }
                if (havefileargs) {
                    if ((*resexec = execv(filepath, fileargs)) == -1 ) {
                        fprintf(stdout, "%s - could not execute %s", getCurrentTime(), filepath);
                        for (int i = 0; i < numfileargs; i++) {
                            fprintf(stdout, " %s", fileargs[i]);
                        }
                        fprintf(stdout, "\n");
                    }
                }
                else {
                    if ((*resexec = execl(filepath, NULL)) == -1) {
                        fprintf(stdout, "%s - could not execute %s\n", getCurrentTime(), filepath);
                    }
                }

                exit(0);
            }
            else {
                pid_t tpid;
                do {
                    tpid = wait(&childstatus);
                } while (tpid != pid);

                if (*resexec == 0) {
                    fprintf(stdout, "%s - program has been executed with pid %d\n", getCurrentTime(), pid);
                }
            }
        }

        remove_last(head);

        pthread_mutex_unlock(&mutex);
    }
}

int main(int argc, char *argv[]) {
    node_t *head = NULL;
    head = (node_t *) malloc(sizeof(node_t));
    if (head == NULL) {
        return 1;
    }
    head->numargs = 0;
    head->flag = 0;

    pthread_t threads[NUM_THREADS];
    pthread_attr_t attr;
    pthread_mutex_init(&mutex,  NULL);
    pthread_cond_init (&cond,  NULL);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr,  PTHREAD_CREATE_JOINABLE);

    /* create the threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], &attr, runner, head);
    }

    int sock_fd, new_fd, port; /* listen on sock_fd, new connection on new_fd */
    struct sockaddr_in my_addr; /* my address information */
    struct sockaddr_in their_addr; /* connector's address information */
    socklen_t sin_size;

    if (argc != 2) {
        fprintf(stderr, "Invalid number of arguments\n");
        fprintf(stderr, "Usage: overseer <port>\n");
        exit(1);
    }

    port = atoi(argv[1]);

    /* generate the socket */
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket");
        exit(1);
    }
    
    /* enable address/port reuse, useful for server development */
    int opt_enable = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt_enable, sizeof(opt_enable));
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEPORT, &opt_enable, sizeof(opt_enable));
    
    /* clear address struct */
    memset(&my_addr, 0, sizeof(my_addr));

    /* generate the end point */
    my_addr.sin_family = AF_INET; /* host byte order */
    my_addr.sin_port = htons(port); /* short, network byte order */
    my_addr.sin_addr.s_addr = INADDR_ANY; /* auto-fill with my IP */

    /* bind the socket to the end point */
    if (bind(sock_fd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1)
    {
        perror("bind");
        exit(1);
    }

    /* start listnening */
    if (listen(sock_fd, BACKLOG) == -1)
    {
        perror("listen");
        exit(1);
    }

    printf("server starts listening ...\n");

    /* repeat: accept, receive, close the connection */
    /* for every accepted connection, use a separate thread to serve it */
    while (1) {
        sin_size = sizeof(struct sockaddr_in);
        if ((new_fd = accept(sock_fd, (struct sockaddr *)&their_addr, &sin_size)) == -1) {
            perror("accept");
            continue;
        }
        
        fprintf(stdout, "%s - connection received from %s\n", getCurrentTime(), inet_ntoa(their_addr.sin_addr));

        push(head, new_fd);

        pthread_cond_signal(&cond);

        close(new_fd);
        
    }

    pthread_attr_destroy(&attr);
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
    pthread_exit(NULL);
}