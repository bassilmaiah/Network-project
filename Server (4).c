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

int authenticate_user(const char *username, const char *password, const char *password_file) {
    FILE *file = fopen(password_file, "r");
    if (!file) {
        perror("Failed to open password file");
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char *file_user = strtok(line, ":");
        char *file_pass = strtok(NULL, "\n");

        if (strcmp(username, file_user) == 0 && strcmp(password, file_pass) == 0) {
            fclose(file);
            return 1;
        }
    }

    fclose(file);
    return 0;
}

void list_files(int client_sock, const char *directory) {
    DIR *dir = opendir(directory);
    struct dirent *entry;

    if (dir == NULL) {
        write(client_sock, "500 Failed to open directory.\n", 30);
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            char file_info[256];
            snprintf(file_info, sizeof(file_info), "%s %ld\n", entry->d_name, entry->d_reclen);
            write(client_sock, file_info, strlen(file_info));
        }
    }
    write(client_sock, ".\n", 2);
    closedir(dir);
}

void send_file(int client_sock, const char *directory, const char *filename) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/%s", directory, filename);
    FILE *file = fopen(filepath, "r");
    
    if (!file) {
        write(client_sock, "404 File not found.\n", 20);
        return;
    }

    char buffer[BUFFER_SIZE];
    while (fgets(buffer, sizeof(buffer), file)) {
        write(client_sock, buffer, strlen(buffer));
    }
    write(client_sock, "\r\n.\r\n", 5);
    fclose(file);
}

void receive_file(int client_sock, const char *directory, const char *filename) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/%s", directory, filename);
    FILE *file = fopen(filepath, "w");

    if (!file) {
        write(client_sock, "400 File cannot save on server side.\n", 37);
        return;
    }

    char buffer[BUFFER_SIZE];
    int total_bytes = 0;
    while (1) {
        int bytes_read = read(client_sock, buffer, sizeof(buffer));
        if (bytes_read <= 0 || strstr(buffer, "\r\n.\r\n")) {
            break;
        }
        fwrite(buffer, 1, bytes_read, file);
        total_bytes += bytes_read;
    }
    fclose(file);
    char response[256];
    snprintf(response, sizeof(response), "200 %d Byte %s file retrieved by server and was saved.\n", total_bytes, filename);
    write(client_sock, response, strlen(response));
}


void *handle_client(void *arg) {
    client_data_t *data = (client_data_t *)arg;
    int client_sock = data->client_sock;
    char *directory = data->directory;
    char *password_file = data->password_file;
    free(data);

    char buffer[BUFFER_SIZE];
    int authenticated = 0;
    char username[256];

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        read(client_sock, buffer, sizeof(buffer) - 1);

        if (strncmp(buffer, "USER", 4) == 0) {
            char *user = strtok(buffer + 5, " ");
            char *pass = strtok(NULL, "\n");
            if (authenticate_user(user, pass, password_file)) {
                authenticated = 1;
                strcpy(username, user);
                write(client_sock, "200 User granted access.\n", 25);
            } else {
                write(client_sock, "400 User not found.\n", 20);
            }
        } else if (authenticated) {
            if (strncmp(buffer, "LIST", 4) == 0) {
                list_files(client_sock, directory);
            } else if (strncmp(buffer, "GET", 3) == 0) {
                char *filename = strtok(buffer + 4, "\n");
                send_file(client_sock, directory, filename);
            } else if (strncmp(buffer, "PUT", 3) == 0) {
                char *filename = strtok(buffer + 4, "\n");
                receive_file(client_sock, directory, filename);
            } else if (strncmp(buffer, "DEL", 3) == 0) {
                char *filename = strtok(buffer + 4, "\n");
                delete_file(client_sock, directory, filename);
            } else if (strncmp(buffer, "QUIT", 4) == 0) {
                write(client_sock, "Goodbye!\n", 9);
                break;
            } else {
                write(client_sock, "500 Unknown command.\n", 21);
            }
        } else {
            write(client_sock, "530 Not authenticated.\n", 23);
        }
    }

    close(client_sock);
    return NULL;
}

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