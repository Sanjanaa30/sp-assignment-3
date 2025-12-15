// client_ops.c
// Part 3 client: supports READ (cat) and WRITE (simple nano-like line editor)
// + displays real-time notifications from server when file is busy.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

// ===== PHASE 4: global socket & SIGINT handler =====
static int g_ops_sockfd = -1;

void client_ops_sigint(int sig)
{
    (void)sig;
    if (g_ops_sockfd >= 0)
        close(g_ops_sockfd);
    printf("\nClient ops exiting cleanly\n");
    exit(0);
}
// ==================================================

static int recv_line(int fd, char *buf, size_t cap)
{
    size_t i = 0;
    while (i + 1 < cap)
    {
        char ch;
        ssize_t r = recv(fd, &ch, 1, 0);
        if (r == 0)
            return 0;   // connection closed
        if (r < 0)
            return -1;  // error
        buf[i++] = ch;
        if (ch == '\n')
            break;
    }
    buf[i] = '\0';
    return 1;
}

static void send_all(int fd, const void *buf, size_t len)
{
    size_t off = 0;
    while (off < len)
    {
        ssize_t n = send(fd, (const char *)buf + off, len - off, 0);
        if (n <= 0)
            return;
        off += (size_t)n;
    }
}

static int connect_to_server(const char *ip, int port)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1)
    {
        fprintf(stderr, "Bad IP address: %s\n", ip);
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("connect");
        close(sockfd);
        return -1;
    }

    /* ===== PHASE 1 PARTIAL: HANDSHAKE ===== */
    char hello[64];
    snprintf(hello, sizeof(hello), "HELLO client_ops_%d\n", getpid());
    send_all(sockfd, hello, strlen(hello));

    char resp[64];
    ssize_t r = recv(sockfd, resp, sizeof(resp) - 1, 0);
    if (r <= 0)
    {
        close(sockfd);
        return -1;
    }
    resp[r] = '\0';

    if (strncmp(resp, "OK", 2) != 0)
    {
        fprintf(stderr, "Handshake failed: %s\n", resp);
        close(sockfd);
        return -1;
    }
    /* ==================================== */

    return sockfd;
}

static void trim_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
    {
        s[n - 1] = '\0';
        n--;
    }
}

/*
 * READ mode (cat equivalent)
 */
static void do_read(const char *ip, int port, const char *filename)
{
    int fd = connect_to_server(ip, port);
    if (fd < 0)
        return;

    /* ===== PHASE 4: track active socket ===== */
    g_ops_sockfd = fd;
    /* ======================================= */

    char header[1024];
    snprintf(header, sizeof(header), "READ %s\n", filename);
    send_all(fd, header, strlen(header));

    char buf[4096];
    for (;;)
    {
        ssize_t r = recv(fd, buf, sizeof(buf), 0);
        if (r == 0)
            break;
        if (r < 0)
        {
            perror("recv");
            break;
        }

        /* ===== PHASE 4: server shutdown handling ===== */
        if (r >= 15 && memcmp(buf, "SERVER_SHUTDOWN", 15) == 0)
        {
            printf("Server is shutting down. Client exiting.\n");
            close(fd);
            exit(0);
        }
        /* ============================================ */

        fwrite(buf, 1, (size_t)r, stdout);
    }

    close(fd);
    g_ops_sockfd = -1;
}

/*
 * WRITE mode (nano-like editor)
 */
static void do_write(const char *ip, int port, const char *filename)
{
    int fd = connect_to_server(ip, port);
    if (fd < 0)
        return;

    /* ===== PHASE 4: track active socket ===== */
    g_ops_sockfd = fd;
    /* ======================================= */

    char header[1024];
    snprintf(header, sizeof(header), "WRITE %s\n", filename);
    send_all(fd, header, strlen(header));

    char line[1024];
    for (;;)
    {
        int rc = recv_line(fd, line, sizeof(line));

        if (rc == 0)
        {
            fprintf(stderr, "Server closed connection while waiting.\n");
            close(fd);
            return;
        }
        if (rc < 0)
        {
            perror("recv_line");
            close(fd);
            return;
        }

        /* ===== PHASE 4: server shutdown handling ===== */
        if (strncmp(line, "SERVER_SHUTDOWN", 15) == 0)
        {
            printf("Server is shutting down. Client exiting.\n");
            close(fd);
            exit(0);
        }
        /* ============================================ */

        if (strncmp(line, "NOTIFY BUSY ", 12) == 0)
        {
            printf("[Notification] %s is currently being edited by another client.\n",
                   line + 12);
            continue;
        }

        if (strncmp(line, "OK WRITE ", 9) == 0)
        {
            printf("Write lock granted. Enter text now.\n");
            printf("Commands: ':wq' = save+quit, ':q!' = quit without saving\n\n");
            break;
        }

        if (strncmp(line, "ERR", 3) == 0)
        {
            printf("%s", line);
            close(fd);
            return;
        }

        printf("%s", line);
    }

    size_t cap = 4096;
    size_t len = 0;
    char *content = (char *)malloc(cap);
    if (!content)
    {
        fprintf(stderr, "Out of memory\n");
        close(fd);
        return;
    }
    content[0] = '\0';

    char input[1024];
    for (;;)
    {
        printf("> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin))
            break;

        trim_newline(input);

        if (strcmp(input, ":q!") == 0)
        {
            printf("Quit without saving.\n");
            free(content);
            close(fd);
            return;
        }
        if (strcmp(input, ":wq") == 0)
            break;

        size_t add = strlen(input) + 1;
        if (len + add + 1 > cap)
        {
            while (len + add + 1 > cap)
                cap *= 2;
            char *tmp = realloc(content, cap);
            if (!tmp)
            {
                fprintf(stderr, "Out of memory\n");
                free(content);
                close(fd);
                return;
            }
            content = tmp;
        }

        memcpy(content + len, input, strlen(input));
        len += strlen(input);
        content[len++] = '\n';
        content[len] = '\0';
    }

    if (len > 0)
        send_all(fd, content, len);

    shutdown(fd, SHUT_WR);

    char reply[1024];
    ssize_t r = recv(fd, reply, sizeof(reply) - 1, 0);
    if (r > 0)
    {
        reply[r] = '\0';
        printf("%s", reply);
    }
    else
    {
        printf("No confirmation from server.\n");
    }

    free(content);
    close(fd);
    g_ops_sockfd = -1;
}

int main()
{
    signal(SIGINT, client_ops_sigint);

    int port = 0;
    char ip[128] = {0};
    char key[64];

    FILE *cfg = fopen("client_ops_conf", "r");
    if (!cfg)
    {
        fprintf(stderr, "Missing client_ops_conf\n");
        return 1;
    }
    if (fscanf(cfg, "%63s %d", key, &port) != 2)
    {
        fclose(cfg);
        return 1;
    }
    if (fscanf(cfg, "%63s %127s", key, ip) != 2)
    {
        fclose(cfg);
        return 1;
    }
    fclose(cfg);

    for (;;)
    {
        printf("\n=== Client Ops Menu ===\n");
        printf("1) Read file (cat)\n");
        printf("2) Write/edit file (nano-like)\n");
        printf("3) Exit\n");
        printf("Choose: ");

        char choice[16];
        if (!fgets(choice, sizeof(choice), stdin))
            break;

        int c = atoi(choice);
        if (c == 3)
            break;

        char filename[512];
        printf("Filename (no slashes, no ..): ");
        if (!fgets(filename, sizeof(filename), stdin))
            break;
        trim_newline(filename);

        if (strlen(filename) == 0)
            continue;

        if (c == 1)
            do_read(ip, port, filename);
        else if (c == 2)
            do_write(ip, port, filename);
        else
            printf("Invalid choice.\n");
    }

    return 0;
}
