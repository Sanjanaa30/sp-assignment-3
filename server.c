// Implementation of TCP connection on server

//imports
#include "server.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Shared directory where server stores all files
#define SHARED_DIR "./shared"

// Structure to pass client socket fd to thread
struct client_ctx {
    int fd; // Client connection file descriptor
};

//file lock linked list
typedef struct FileLockNode {
    char *name;
    pthread_rwlock_t rw;
    struct FileLockNode *next;
} FileLockNode;

// Head of lock list
static FileLockNode *g_locks = NULL;

// Mutex
static pthread_mutex_t g_locks_mu = PTHREAD_MUTEX_INITIALIZER;

// Ensure shared directory exists
static void shared_dir(void) {
    struct stat st;
    if (stat(SHARED_DIR, &st) == -1) {
        // owner only permissions
        mkdir(SHARED_DIR, 0700);
    }
} 

//rwlock for a given filename
static pthread_rwlock_t *get_file_rwlock(const char *filename) {
    //Mutex Lock to lock gloabal list
    pthread_mutex_lock(&g_locks_mu);

    FileLockNode *cur = g_locks;
    while (cur) {
        if (strcmp(cur->name, filename) == 0) {
            // Unlock global list mutex
            pthread_mutex_unlock(&g_locks_mu);
            // Return address of rwlock
            return &cur->rw;
        }
        // Move to next node
        cur = cur->next;
    }

    FileLockNode *node = (FileLockNode *)calloc(1, sizeof(*node));
    node->name = strdup(filename);

    // Initialize rwlock
    pthread_rwlock_init(&node->rw, NULL);
    
    // Insert node at head of list
    node->next = g_locks;
    g_locks = node;
    
    // Unlock global list mutex
    pthread_mutex_unlock(&g_locks_mu);
    return &node->rw;
}

// Receive one line ending with newline from socket
static int recv_line(int fd, char *buf, size_t cap) {
    size_t i = 0;
    while (i + 1 < cap) {
        char ch;
        ssize_t r = recv(fd, &ch, 1, 0);
        if (r == 0) return 0;     // connection closed
        if (r < 0) return -1;     // error
        if (ch == '\n') break;
        buf[i++] = ch;
    }
    buf[i] = '\0';
    return 1;
}

// Handle READ command for one client
static void *handle_read(int connection, pthread_rwlock_t *rw, const char *filename) {
    //Test
    printf("[T%lu] waiting RDLOCK %s\n", (unsigned long)pthread_self(), filename);
    
    // Acquire read lock so multiple readers can read together
    pthread_rwlock_rdlock(rw);
    
    //Test
    printf("[T%lu] acquired RDLOCK %s\n", (unsigned long)pthread_self(), filename);
    
    //Test
    usleep(200000);
    
    //Buffer path
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", SHARED_DIR, filename);
    

    //Open file for reading
    FILE *in = fopen(path, "rb");

    if (!in) {
        printf("[T%lu] releasing RDLOCK %s\n", (unsigned long)pthread_self(), filename);
        // Release read lock
        pthread_rwlock_unlock(rw);

        const char *msg = "ERR file not found\n";

        // Send error to client
        send(connection, msg, strlen(msg), 0);

        // Close connection
        close(connection);

        //Ends thread
        return NULL;
    }
    
    // Buffer used to send file data
    char buf2[65536];

    // Number of bytes read from file
    size_t nread;


    while ((nread = fread(buf2, 1, sizeof(buf2), in)) > 0) {
        if (send(connection, buf2, nread, 0) < 0) break;

        //Test
        usleep(20000);
    }
    
    // Close file
    fclose(in);

    // Release read lock
    pthread_rwlock_unlock(rw);
    close(connection);
    return NULL;

}

// Handle WRITE command for one client
static void *handle_write(int connection, pthread_rwlock_t *rw, const char *filename, const char *confirmation) {
    
    // Buffer used to receive file data from client
    char client_data_buf[65536];
    
    //Test
    printf("[T%lu] waiting WRLOCK %s\n", (unsigned long)pthread_self(), filename);
    
    // Acquire write lock so only one writer can write
    pthread_rwlock_wrlock(rw);
    
    //Test
    printf("[T%lu] acquired WRLOCK %s\n", (unsigned long)pthread_self(), filename);

    //usleep(300000);

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", SHARED_DIR, filename);
    
    // Open file for writing binary and overwrite
    FILE *out = fopen(path, "wb");

    if (!out) {
        perror("Failed to open the file in the server");
        pthread_rwlock_unlock(rw);
        close(connection);
        return NULL;
    }
    
    // Print where the server is saving the file
    printf("Saving to '%s'...\n", path);

    for (;;) {
        // Receive chunk from client
        ssize_t n = recv(connection, client_data_buf, sizeof(client_data_buf), 0);
        if (n > 0) {
            size_t w = fwrite(client_data_buf, 1, (size_t)n, out);
            if (w != (size_t)n) {
                perror("fwrite");
                fclose(out);
                pthread_rwlock_unlock(rw);
                close(connection);
                return NULL;
            }

            //usleep(20000);//usleep

            } else if (n == 0) {
                // client finished sending
                break;
            } else { //error
                perror("recv");
                fclose(out);
                pthread_rwlock_unlock(rw);
                close(connection);
                return NULL;
        }
    }
    // Close output file
    fclose(out);

    // Release write lock after finishing write
    pthread_rwlock_unlock(rw);
    
    // Send confirmation to client
    if (send(connection, confirmation, strlen(confirmation), 0) < 0) {
        perror("Confirmation");
    }

    close(connection);
    printf("Client done: file '%s' received\n", filename);
    return NULL;
}

// Thread entry point 
static void *handle_client(void *arg) {
    // Cast thread argument
    struct client_ctx *ctx = (struct client_ctx *)arg;

    int connection = ctx->fd;
    free(ctx);  // Free context memory
    
    // Confirmation message for WRITE
    const char *confirmation = "File Received by server\n";

    // Line buffer 
    char line[1024];

    // Receive one header line from client
    int rc = recv_line(connection, line, sizeof(line));

     // If failed to read header
    if (rc <= 0) {
        close(connection);
        return NULL;
    }

    // Command buffer
    char cmd[16];
    char filename[512];

    // Parse header into cmd and filename
    if (sscanf(line, "%15s %511s", cmd, filename) != 2) {
        const char *msg = "ERR bad header. Use: READ <file> or WRITE <file>\n";
        send(connection, msg, strlen(msg), 0);
        close(connection);
        return NULL;
    }

    // Reject unsafe filenames
    if (strstr(filename, "..") != NULL || strchr(filename, '/') != NULL) {
        const char *msg = "ERR invalid filename\n";
        send(connection, msg, strlen(msg), 0);
        close(connection);
        return NULL;
    }


    //readlock 
    pthread_rwlock_t *rw = get_file_rwlock(filename);

    // If command is not READ and not WRITE
    if (strcmp(cmd, "READ") != 0 && strcmp(cmd, "WRITE") != 0) {
        const char *msg = "ERR unknown command. Use READ or WRITE\n";
        send(connection, msg, strlen(msg), 0);
        close(connection);
        return NULL;
    }

    // Call Read handler
    if (strcmp(cmd, "READ") == 0) {
        return handle_read(connection, rw, filename);
    }
    
     // Call Write handler
    if (strcmp(cmd, "WRITE") == 0) {
        return handle_write(connection, rw, filename, confirmation);
    }
    
    //Error
    const char *msg = "ERR unknown command. Use READ or WRITE\n";
    send(connection, msg, strlen(msg), 0);
    close(connection);
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

    shared_dir(); 

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
