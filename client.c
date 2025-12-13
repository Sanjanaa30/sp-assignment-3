// Implementation of TCP connection on client

#include "client.h"



static const char* base_name(const char* p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static void send_all(int fd, const void *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, (const char*)buf + off, len - off, 0);
        if (n <= 0) return;         
        off += (size_t)n;
    }
}

//Let me capture the packets 


int main() {
    int port;
    char server_IP[64] = "";
    char file_path[2048] = "";
    char buf[64];

    struct timespec t0,t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    uint64_t bytes_sent =0;


    //Read the config file
    FILE *cfg = fopen("client_conf", "r");
    if (!cfg) return 1;
    fscanf(cfg, "%s %d", buf, &port);                 
    fscanf(cfg, "%s %s", buf, server_IP);          
    fscanf(cfg, "%s %s", buf, file_path);         
    fclose(cfg);

    // Open the data file from file path..
    FILE *in = fopen(file_path, "rb");
    if (!in) return 1;

    // Make socket & connect
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return 1;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((unsigned short)port);
    inet_pton(AF_INET, server_IP, &addr.sin_addr);
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) return 1;

    // Send the file name
    const char *fname = base_name(file_path);
    char header[1024];
    int hl = snprintf(header, sizeof(header), "%s\n", fname);
    send_all(sockfd, header, (size_t)hl);

    // Stream file bytes
    char sendbuf[65536];
    size_t n;
    while ((n = fread(sendbuf, 1, sizeof(sendbuf), in)) > 0) {
        send_all(sockfd, sendbuf, n);
        bytes_sent += (uint64_t)n;
    }
    fclose(in);

    shutdown(sockfd, SHUT_WR);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    char reply[128];
    ssize_t r = recv(sockfd, reply, sizeof(reply) - 1, 0);
    if (r > 0) {
        reply[r] = '\0';
        printf("%s", reply);
    }
    else{
        printf("No Confirmation from the server\n");
    }

    //From the total sent bytes 
    double dt = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec)/1e9;
    double MB  = bytes_sent / 1e6;
    printf("TCP: sent %" PRIu64 " bytes in %.3f s (%.2f MB/s)\n", bytes_sent, dt, MB / dt);



    close(sockfd);
    return 0;
}
