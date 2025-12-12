// Implementation of TCP connection on server

#include "server.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct client_ctx {
    int fd;
};

static void *handle_client(void *arg) {
    struct client_ctx *ctx = (struct client_ctx *)arg;
    int connection = ctx->fd;
    free(ctx);

    char client_data_buf[65536];
    char filename[512];
    size_t name_len = 0;
    const char *confirmation = "File Received by server\n";

    for (;;) {
        char ch;
        ssize_t r = recv(connection, &ch, 1, 0);
        if (r == 0) {
            fprintf(stderr, "Client closed the connection while sending filename\n");
            close(connection);
            return NULL;
        }
        if (r < 0) {
            perror("Receiving filename from client failed");
            close(connection);
            return NULL;
        }
        if (ch == '\n') {
            filename[name_len] = '\0';
            break;
        }
        if (name_len + 1 >= sizeof(filename)) {
            fprintf(stderr, "Filename too long\n");
            close(connection);
            return NULL;
        }

        filename[name_len++] = ch;
    }

    FILE *out = fopen(filename, "wb");
    if (!out) {
        perror("Failed to open the file in the server");
        close(connection);
        return NULL;
    }

    printf("Saving to '%s'...\n", filename);

    for (;;) {
        ssize_t n = recv(connection, client_data_buf, sizeof(client_data_buf), 0);
        if (n > 0) {
            size_t w = fwrite(client_data_buf, 1, (size_t)n, out);
            if (w != (size_t)n) {
                perror("fwrite");
                fclose(out);
                close(connection);
                return NULL;
            }
        } else if (n == 0) {
            // client finished sending
            break;
        } else {
            perror("recv");
            fclose(out);
            close(connection);
            return NULL;
        }
    }

    fclose(out);

    if (send(connection, confirmation, strlen(confirmation), 0) < 0) {
        perror("Confirmation");
    }

    close(connection);
    printf("Client done: file '%s' received\n", filename);
    return NULL;
}

int main() {
    int port;
    char buf[64];
    struct sockaddr_in addr = {0};
    int backlog = 10;
    FILE *server_config = fopen("server_conf", "r");
    if (server_config == NULL) {
        printf("Please, check the server configuration file\n");
        return -1;
    } else {
        if (fscanf(server_config, "%s %d", buf, &port) != 2) {
            printf("Invalid server_conf format\n");
            fclose(server_config);
            return -1;
        }
        printf("Server will be Listening to the Port : %d\n", port);
    }
    fclose(server_config);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation error");
        return -1;
    }

    int yes = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Error in bind");
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, backlog) < 0) {
        perror("Server failed Listen");
        close(sockfd);
        return -1;
    }

    printf("Server is Listening on the Port %d...\n", port);

    while (1) {
        int connection = accept(sockfd, NULL, NULL);
        if (connection < 0) {
            perror("Accept failed");
            continue;  // don’t exit; try next client
        }

        printf("New client connected\n");

        struct client_ctx *ctx = malloc(sizeof(*ctx));
        if (!ctx) {
            fprintf(stderr, "Out of memory\n");
            close(connection);
            continue;
        }
        ctx->fd = connection;

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, ctx) != 0) {
            perror("pthread_create");
            close(connection);
            free(ctx);
            continue;
        }

        pthread_detach(tid);
    }

    close(sockfd);
    return 0;
}
