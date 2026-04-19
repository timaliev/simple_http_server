// simple_http_server_mt.c
// vim:set mt=c
// simple_http_mt.c
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFFER_SIZE 8192
#define ROOT_DIR "."
#define DEFAULT_PORT 80

typedef struct {
    int client_fd;
} client_arg_t;

/* Global statistics structure */
typedef struct {
    int total_threads_started;
    int total_threads_finished;
    int thread_failures;
    unsigned long long total_work_units;
    pthread_mutex_t stats_mutex;
} Statistics;

Statistics stats = {0, 0, 0, 0, PTHREAD_MUTEX_INITIALIZER};

/* Atomic counter for active threads */
atomic_int active_threads = 0;

/* Flag shared between main and signal handler to stop on signal */
volatile sig_atomic_t keep_running = 1;

/* Signal handler */
void handle_sigint(int signum) {
    if (signum == SIGINT || signum == SIGTERM ) {
        const char *name = sigabbrev_np(signum);
        printf("\nSignal received, stopping new thread creation...\n");
        keep_running = 0;
    }
}

const char* get_mime_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    ext++;

    if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0) return "text/html";
    if (strcasecmp(ext, "txt") == 0) return "text/plain";
    if (strcasecmp(ext, "css") == 0) return "text/css";
    if (strcasecmp(ext, "js") == 0) return "application/javascript";
    if (strcasecmp(ext, "json") == 0) return "application/json";
    if (strcasecmp(ext, "png") == 0) return "image/png";
    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, "gif") == 0) return "image/gif";
    if (strcasecmp(ext, "svg") == 0) return "image/svg+xml";
    if (strcasecmp(ext, "ico") == 0) return "image/x-icon";
    if (strcasecmp(ext, "pdf") == 0) return "application/pdf";
    if (strcasecmp(ext, "xml") == 0) return "application/xml";
    return "application/octet-stream";
}

void send_response(int client, int status, const char* status_text, const char* mime,const char* body, size_t body_len) {
    char header[1024];
    int hdr_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_text, mime, body_len);

    write(client, header, hdr_len);
    if (body_len > 0 && body)
        write(client, body, body_len);
}

void* handle_client(void* arg) {
    client_arg_t* carr = (client_arg_t*)arg;
    int client = carr->client_fd;
    free(carr);

    char buf[BUFFER_SIZE] = {0};
    ssize_t n = read(client, buf, sizeof(buf)-1);
    if (n <= 0) {
        close(client);
        pthread_exit(NULL);
    }

    char method[16], path[512], proto[16];
    if (sscanf(buf, "%15s %511s %15s", method, path, proto) != 3 ||
        strcmp(method, "GET") != 0) {
        send_response(client, 400, "Bad Request", "text/plain",
                      "Bad Request", 13);
        close(client);
        pthread_exit(NULL);
    }

    char fullpath[1024];
    if (strcmp(path, "/") == 0)
        snprintf(fullpath, sizeof(fullpath), "%s/index.html", ROOT_DIR);
    else
        snprintf(fullpath, sizeof(fullpath), "%s%s", ROOT_DIR, path);

    if (strstr(path, "..")) {
        send_response(client, 403, "Forbidden", "text/plain",
                      "Forbidden", 9);
        close(client);
        pthread_exit(NULL);
    }

    struct stat st;
    if (stat(fullpath, &st) != 0 || S_ISDIR(st.st_mode)) {
        char idx[1024];
        snprintf(idx, sizeof(idx), "%s/index.html", fullpath);
        if (stat(idx, &st) == 0 && S_ISREG(st.st_mode))
            strncpy(fullpath, idx, sizeof(fullpath)-1);
        else {
            send_response(client, 404, "Not Found", "text/plain", "404 Not Found", 13);
            close(client);
            pthread_exit(NULL);
        }
    }

    FILE* f = fopen(fullpath, "rb");
    if (!f) {
        send_response(client, 404, "Not Found", "text/plain",
                      "404 Not Found", 13);
        close(client);
        pthread_exit(NULL);
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* body = malloc(size);
    if (!body) {
        fclose(f);
        close(client);
        pthread_exit(NULL);
    }

    fread(body, 1, size, f);
    fclose(f);

    const char* mime = get_mime_type(fullpath);
    send_response(client, 200, "OK", mime, body, (size_t)size);

    free(body);
    close(client);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;

    if (argc > 1) {
        char *endptr;
        long p = strtol(argv[1], &endptr, 10);
        if (*endptr != '\0' || p <= 0 || p > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[1]);
            fprintf(stderr, "Usage: %s [port]\n", argv[0]);
            return 1;
        }
        port = (int)p;
    }

    int server_fd;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        fprintf(stderr, "Failed to bind to port %d (may need root for ports < 1024)\n", port);
        exit(1);
    }
    if (listen(server_fd, 128) < 0) { perror("listen"); exit(1); }

    printf("Multithreaded HTTP server listening on port %d\n", port);

    while (1) {
        int client = accept(server_fd, (struct sockaddr*)&addr, &addr_len);
        if (client < 0) continue;

        client_arg_t* carr = malloc(sizeof(client_arg_t));
        if (!carr) {
            close(client);
            continue;
        }
        carr->client_fd = client;

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, carr) != 0) {
            free(carr);
            close(client);
            continue;
        }
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}
