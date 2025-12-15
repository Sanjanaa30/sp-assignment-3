// Implementation of TCP connection on server

// imports
#define _POSIX_C_SOURCE 200809L
#include "server.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

/* ============================================================
 * PHASE 4: Client tracking for graceful shutdown
 * ============================================================ */
#define MAX_CLIENTS 128

static int client_fds[MAX_CLIENTS]; // stores active client sockets
static int client_count = 0;        // number of active clients
static pthread_mutex_t client_mu = PTHREAD_MUTEX_INITIALIZER;
/* ============================================================ */

// Shared directory where server stores all files
#define SHARED_DIR "./shared"

/* ============================================================
 * PHASE 4: Global shutdown flag & SIGINT handler
 * ============================================================ */
volatile sig_atomic_t server_running = 1;

void handle_sigint(int sig)
{
    (void)sig;          // suppress unused warning
    server_running = 0; // tell main accept-loop to exit
}
/* ============================================================ */

// Structure to pass client socket fd to thread
struct client_ctx
{
    int fd; // Client connection file descriptor
};

// file lock linked list
typedef struct FileLockNode
{
    char *name;
    pthread_rwlock_t rw;
    struct FileLockNode *next;
} FileLockNode;

// Head of lock list
static FileLockNode *g_locks = NULL;

// Mutex
static pthread_mutex_t g_locks_mu = PTHREAD_MUTEX_INITIALIZER;

// Ensure shared directory exists
static void shared_dir(void)
{
    struct stat st;
    if (stat(SHARED_DIR, &st) == -1)
    {
        // owner only permissions
        mkdir(SHARED_DIR, 0700);
    }
}

// rwlock for a given filename
static pthread_rwlock_t *get_file_rwlock(const char *filename)
{
    // Mutex Lock to lock gloabal list
    pthread_mutex_lock(&g_locks_mu);

    FileLockNode *cur = g_locks;
    while (cur)
    {
        if (strcmp(cur->name, filename) == 0)
        {
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
static int recv_line(int fd, char *buf, size_t cap)
{
    size_t i = 0;
    while (i + 1 < cap)
    {
        char ch;
        ssize_t r = recv(fd, &ch, 1, 0);
        if (r == 0)
            return 0; // connection closed
        if (r < 0)
            return -1; // error
        if (ch == '\n')
            break;
        buf[i++] = ch;
    }
    buf[i] = '\0';
    return 1;
}

// Handle READ command for one client
static void *handle_read(int connection, pthread_rwlock_t *rw, const char *filename)
{
    // Test
    printf("[T%lu] waiting RDLOCK %s\n", (unsigned long)pthread_self(), filename);

    // Acquire read lock so multiple readers can read together
    pthread_rwlock_rdlock(rw);

    // Test
    printf("[T%lu] acquired RDLOCK %s\n", (unsigned long)pthread_self(), filename);

    // Test
    nanosleep(&(struct timespec){0, 200000000}, NULL);

    // Buffer path
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", SHARED_DIR, filename);

    // Open file for reading
    FILE *in = fopen(path, "rb");

    if (!in)
    {
        printf("[T%lu] releasing RDLOCK %s\n", (unsigned long)pthread_self(), filename);
        // Release read lock
        pthread_rwlock_unlock(rw);

        const char *msg = "ERR file not found\n";

        // Send error to client
        send(connection, msg, strlen(msg), 0);

        // Close connection
        close(connection);

        // Ends thread
        return NULL;
    }

    // Buffer used to send file data
    char buf2[65536];

    // Number of bytes read from file
    size_t nread;

    while ((nread = fread(buf2, 1, sizeof(buf2), in)) > 0)
    {
        if (send(connection, buf2, nread, 0) < 0)
            break;

        // Test
        nanosleep(&(struct timespec){0, 20000000}, NULL);
    }

    // Close file
    fclose(in);

    // Release read lock
    pthread_rwlock_unlock(rw);
    close(connection);
    return NULL;
}

// Handle WRITE command for one client
static void *handle_write(int connection, pthread_rwlock_t *rw, const char *filename, const char *confirmation)
{

    // Test
    printf("[T%lu] waiting WRLOCK %s\n", (unsigned long)pthread_self(), filename);

    /*
     * Part 3: Real-time notifications when file is already being edited.
     * We try to acquire WRLOCK. If busy, notify client immediately.
     */
    for (;;)
    {
        int trc = pthread_rwlock_trywrlock(rw);
        if (trc == 0)
            break; // acquired
        // Busy (likely EBUSY). Notify client and retry.
        char note[1024];
        snprintf(note, sizeof(note), "NOTIFY BUSY %s\n", filename);
        send(connection, note, strlen(note), 0);
        nanosleep(&(struct timespec){0, 200000000}, NULL); // 200ms retry; keeps "real-time" feel
    }

    printf("[T%lu] acquired WRLOCK %s\n", (unsigned long)pthread_self(), filename);

    // Tell client it can start sending file contents now
    {
        char ok[1024];
        snprintf(ok, sizeof(ok), "OK WRITE %s\n", filename);
        send(connection, ok, strlen(ok), 0);
    }

    // usleep(300000);

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", SHARED_DIR, filename);

    // Open file for writing binary and overwrite
    FILE *out = fopen(path, "wb");

    if (!out)
    {
        perror("Failed to open the file in the server");
        pthread_rwlock_unlock(rw);
        close(connection);
        return NULL;
    }

    // Print where the server is saving the file
    printf("Saving to '%s'...\n", path);

    char buf[65536];
    ssize_t r;
    while ((r = recv(connection, buf, sizeof(buf), 0)) > 0)
    {
        fwrite(buf, 1, r, out);
    }

    // Close output file
    fclose(out);

    // Release write lock after finishing write
    pthread_rwlock_unlock(rw);

    // Send confirmation to client
    if (send(connection, confirmation, strlen(confirmation), 0) < 0)
    {
        perror("Confirmation");
    }

    close(connection);
    printf("Client done: file '%s' received\n", filename);
    return NULL;
}

// Thread entry point
static void *handle_client(void *arg)
{
    // Cast thread argument
    struct client_ctx *ctx = (struct client_ctx *)arg;

    int connection = ctx->fd;
    free(ctx); // Free context memory

    const char *confirmation = "File Received by server\n";

    // Line buffer
    char line[1024];

    // ===== PHASE 1 PARTIAL: HANDSHAKE =====
    if (recv_line(connection, line, sizeof(line)) <= 0 ||
        strncmp(line, "HELLO", 5) != 0)
    {
        send(connection, "ERR Handshake required\n", 24, 0);
        close(connection);
        return NULL;
    }
    send(connection, "OK\n", 3, 0);
    // ====================================

    // Read actual command
    if (recv_line(connection, line, sizeof(line)) <= 0)
    {
        close(connection);
        return NULL;
    }

    // Parse header into cmd and filename
    char cmd[16], filename[512];
    if (sscanf(line, "%15s %511s", cmd, filename) != 2)
    {
        send(connection, "ERR bad header\n", 15, 0);
        close(connection);
        return NULL;
    }

    // Reject unsafe filenames
    if (strstr(filename, "..") != NULL || strchr(filename, '/') != NULL)
    {
        const char *msg = "ERR invalid filename\n";
        send(connection, msg, strlen(msg), 0);
        close(connection);
        return NULL;
    }

    // readlock
    pthread_rwlock_t *rw = get_file_rwlock(filename);

    // If command is not READ and not WRITE
    if (strcmp(cmd, "READ") != 0 && strcmp(cmd, "WRITE") != 0)
    {
        const char *msg = "ERR unknown command. Use READ or WRITE\n";
        send(connection, msg, strlen(msg), 0);
        close(connection);
        return NULL;
    }

    // Call Read handler
    if (strcmp(cmd, "READ") == 0)
    {
        return handle_read(connection, rw, filename);
    }

    // Call Write handler
    if (strcmp(cmd, "WRITE") == 0)
    {
        return handle_write(connection, rw, filename, confirmation);
    }

    // Error
    const char *msg = "ERR unknown command. Use READ or WRITE\n";
    send(connection, msg, strlen(msg), 0);
    close(connection);
    return NULL;
}

int main()
{
    signal(SIGINT, handle_sigint); // ===== PHASE 4 ADD =====

    int port;
    char buf[64];
    struct sockaddr_in addr = {0};
    int backlog = 10;
    FILE *server_config = fopen("server_conf", "r");

    if (server_config == NULL)
    {
        printf("Please, check the server configuration file\n");
        return -1;
    }
    else
    {
        if (fscanf(server_config, "%s %d", buf, &port) != 2)
        {
            printf("Invalid server_conf format\n");
            fclose(server_config);
            return -1;
        }
        printf("Server will be Listening to the Port : %d\n", port);
    }
    fclose(server_config);

    shared_dir();

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("Socket creation error");
        return -1;
    }

    int yes = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("Error in bind");
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, backlog) < 0)
    {
        perror("Server failed Listen");
        close(sockfd);
        return -1;
    }

    printf("Server is Listening on the Port %d...\n", port);

    while (server_running)
    {
        int connection = accept(sockfd, NULL, NULL);
        if (connection < 0)
        {
            if (!server_running)
                break;
            if (errno == EINTR)
                continue;
            perror("Accept failed");
            continue;
        }
        printf("New client connected\n");

        /* ===== PHASE 4: track connected client ===== */
        pthread_mutex_lock(&client_mu);
        if (client_count < MAX_CLIENTS)
            client_fds[client_count++] = connection;
        pthread_mutex_unlock(&client_mu);
        /* =========================================== */

        struct client_ctx *ctx = malloc(sizeof(*ctx));

        if (!ctx)
        {
            fprintf(stderr, "Out of memory\n");
            close(connection);
            continue;
        }
        ctx->fd = connection;

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, ctx) != 0)
        {
            perror("pthread_create");
            close(connection);
            free(ctx);
            continue;
        }

        pthread_detach(tid);
    }

    /* ===== PHASE 4: notify all clients on shutdown ===== */
    pthread_mutex_lock(&client_mu);
    for (int i = 0; i < client_count; i++)
    {
        send(client_fds[i], "SERVER_SHUTDOWN\n", 16, 0);
        close(client_fds[i]);
    }
    client_count = 0;
    pthread_mutex_unlock(&client_mu);
    /* ================================================== */

    close(sockfd);
    printf("Server shut down cleanly\n");
    return 0;
}
