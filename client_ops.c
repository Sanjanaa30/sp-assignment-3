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

static int recv_line(int fd, char *buf, size_t cap) {
    size_t i = 0;
    while (i + 1 < cap) {
        char ch;
        ssize_t r = recv(fd, &ch, 1, 0);
        if (r == 0) return 0;      // closed
        if (r < 0) return -1;      // error
        buf[i++] = ch;
        if (ch == '\n') break;
    }
    buf[i] = '\0';
    return 1;
}

static void send_all(int fd, const void *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, (const char*)buf + off, len - off, 0);
        if (n <= 0) return;
        off += (size_t)n;
    }
}

static int connect_to_server(const char *ip, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((unsigned short)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "Bad IP address: %s\n", ip);
        close(sockfd);
        return -1;
    }
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }
    return sockfd;
}

static void trim_newline(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) {
        s[n-1] = '\0';
        n--;
    }
}

/*
 * READ mode (cat equivalent):
 * - send: "READ <filename>\n"
 * - print everything until server closes connection
 */
static void do_read(const char *ip, int port, const char *filename) {
    int fd = connect_to_server(ip, port);
    if (fd < 0) return;

    char header[1024];
    snprintf(header, sizeof(header), "READ %s\n", filename);
    send_all(fd, header, strlen(header));

    // Server might send "ERR ..." (as plain text) or raw file bytes.
    // We just print everything we receive to stdout.
    char buf[4096];
    for (;;) {
        ssize_t r = recv(fd, buf, sizeof(buf), 0);
        if (r == 0) break;
        if (r < 0) { perror("recv"); break; }
        fwrite(buf, 1, (size_t)r, stdout);
    }

    close(fd);
}

/*
 * WRITE mode (nano-like simple line editor):
 * - connect
 * - send "WRITE <filename>\n"
 * - wait for:
 *    - "NOTIFY BUSY <filename>\n" => print notification and keep waiting
 *    - "OK WRITE <filename>\n"    => start editor, then send content
 *
 * Editor:
 * - user types lines
 * - type ":wq" on a new line to save+quit
 * - type ":q!" to quit without saving
 */
static void do_write(const char *ip, int port, const char *filename) {
    int fd = connect_to_server(ip, port);
    if (fd < 0) return;

    char header[1024];
    snprintf(header, sizeof(header), "WRITE %s\n", filename);
    send_all(fd, header, strlen(header));

    // Wait for server notifications / OK
    char line[1024];
    for (;;) {
        int rc = recv_line(fd, line, sizeof(line));
        if (rc == 0) {
            fprintf(stderr, "Server closed connection while waiting.\n");
            close(fd);
            return;
        }
        if (rc < 0) {
            perror("recv_line");
            close(fd);
            return;
        }

        // Example: "NOTIFY BUSY test.txt\n"
        if (strncmp(line, "NOTIFY BUSY ", 12) == 0) {
            // Real-time notification
            printf("[Notification] %s is currently being edited by another client.\n",
                   line + 12);
            // remove trailing newline for cleaner printing
            // (optional) but fine as-is
            continue;
        }

        // Example: "OK WRITE test.txt\n"
        if (strncmp(line, "OK WRITE ", 9) == 0) {
            printf("Write lock granted. Enter text now.\n");
            printf("Commands: ':wq' = save+quit, ':q!' = quit without saving\n\n");
            break;
        }

        // Some servers might send ERR lines
        if (strncmp(line, "ERR", 3) == 0) {
            printf("%s", line);
            close(fd);
            return;
        }

        // Unknown line — print and keep going
        printf("%s", line);
    }

    // Simple line editor: collect lines into a dynamic buffer
    size_t cap = 4096;
    size_t len = 0;
    char *content = (char*)malloc(cap);
    if (!content) {
        fprintf(stderr, "Out of memory\n");
        close(fd);
        return;
    }
    content[0] = '\0';

    char input[1024];
    for (;;) {
        printf("> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) {
            printf("\nEnd of input.\n");
            break;
        }
        trim_newline(input);

        if (strcmp(input, ":q!") == 0) {
            printf("Quit without saving.\n");
            free(content);
            close(fd);
            return;
        }
        if (strcmp(input, ":wq") == 0) {
            printf("Saving...\n");
            break;
        }

        // append line + '\n'
        size_t add = strlen(input) + 1;
        if (len + add + 1 > cap) {
            while (len + add + 1 > cap) cap *= 2;
            char *tmp = (char*)realloc(content, cap);
            if (!tmp) {
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

    // Send content bytes to server
    if (len > 0) send_all(fd, content, len);

    // Close write side so server knows we're done
    shutdown(fd, SHUT_WR);

    // Read final server confirmation (your server sends "File Received by server\n")
    char reply[1024];
    ssize_t r = recv(fd, reply, sizeof(reply) - 1, 0);
    if (r > 0) {
        reply[r] = '\0';
        printf("%s", reply);
    } else {
        printf("No confirmation from server.\n");
    }

    free(content);
    close(fd);
}

int main() {
    // Read config like your existing code style:
    // client_ops_conf:
    // PORT_NO 8449
    // SERVER_IP 127.0.0.1
    int port = 0;
    char ip[128] = {0};
    char key[64];

    FILE *cfg = fopen("client_ops_conf", "r");
    if (!cfg) {
        fprintf(stderr, "Missing client_ops_conf\n");
        return 1;
    }
    if (fscanf(cfg, "%63s %d", key, &port) != 2) { fclose(cfg); return 1; }
    if (fscanf(cfg, "%63s %127s", key, ip) != 2) { fclose(cfg); return 1; }
    fclose(cfg);

    for (;;) {
        printf("\n=== Client Ops Menu ===\n");
        printf("1) Read file (cat)\n");
        printf("2) Write/edit file (nano-like)\n");
        printf("3) Exit\n");
        printf("Choose: ");

        char choice[16];
        if (!fgets(choice, sizeof(choice), stdin)) break;

        int c = atoi(choice);
        if (c == 3) break;

        char filename[512];
        printf("Filename (no slashes, no ..): ");
        if (!fgets(filename, sizeof(filename), stdin)) break;
        trim_newline(filename);

        if (strlen(filename) == 0) continue;

        if (c == 1) do_read(ip, port, filename);
        else if (c == 2) do_write(ip, port, filename);
        else printf("Invalid choice.\n");
    }

    return 0;
}
