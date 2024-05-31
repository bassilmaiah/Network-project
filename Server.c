#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>

#define BUFFER_SIZE 1024

typedef struct {
    int client_sock;
    char *directory;
    char *password_file;
} client_data_t;


int main(int argc, char *argv[]) {
    char *directory = NULL;
    int port = 0;
    char *password_file = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "d:p:u:")) != -1) {
        switch (opt) {
            case 'd':
                directory = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'u':
                password_file = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s -d directory -p port -u password_file\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (!directory || !port || !password_file) {
        fprintf(stderr, "All arguments -d, -p, -u are required.\n");
        exit(EXIT_FAILURE);
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, 5) < 0) {
        perror("Listen failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("File server listening on localhost port %d\n", port);

    while (1) {
        int *client_sock = malloc(sizeof(int));
        *client_sock = accept(sockfd, NULL, NULL);
        if (*client_sock < 0) {
            perror("Accept failed");
            free(client_sock);
            continue;
        }

        pthread_t tid;
        client_data_t *data = malloc(sizeof(client_data_t));
        data->client_sock = *client_sock;
        data->directory = directory;
        data->password_file = password_file;

        pthread_create(&tid, NULL, handle_client, data);
        pthread_detach(tid);
    }

    close(sockfd);
    return 0;
}
///