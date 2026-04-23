// simple_http_server_mt.c
// vim:set ft=c
//
#include <stddef.h>
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifndef __linux__
#warning "Some process statistics not implemented on this platform"
#endif

typedef struct {
  char state;
  long pid;
  // char comm[17];
  long ppid, pgrp, session, tty_nr;
  int flags;
  unsigned long long minflt, cminflt, majflt, cmajflt;
  unsigned long long utime, stime, cutime, cstime;
  long priority, nice, num_threads;
  unsigned long long start_time;
  unsigned long long vsize; // virtual memory size (bytes)
  long rss;                 // resident size (pages)
} proc_stat;

/// Get rich runtime stats by parsing `/proc/self/stat` on Linux
int read_proc_stat(proc_stat *out) {
#ifdef __linux__
  char path[64];
  snprintf(path, sizeof(path), "/proc/self/stat");

  FILE *f = fopen(path, "r");
  if (!f)
    return -1;

  char line[1024];
  if (!fgets(line, sizeof(line), f)) {
    fclose(f);
    return -1;
  }
  fclose(f);

  // Find the closing `)` of the comm field
  char *closing = strrchr(line, ')');
  if (!closing)
    return -1;

  // Skip any space after `)`
  char *rest = closing + 1;
  while (*rest == ' ')
    rest++;

  // fprintf(stderr, "DEBUG: ************ read_proc_stat() line 67. OK here. sizeof rest=%lu\n%s\n", sizeof(rest), rest);
  // Parse all fields from the tail (rest of the line is numeric)
  // int res = sscanf(
  //     rest,
  //     "%c %ld %ld %ld %ld %d %llu %llu %llu %llu %llu %llu %llu %llu %ld %ld %ld %llu %llu %ld",
  //     &out->state, &out->ppid, &out->pgrp, &out->session, &out->tty_nr,
  //     &out->flags, &out->minflt, &out->cminflt, &out->majflt, &out->cmajflt,
  //     &out->utime, &out->stime, &out->cutime, &out->cstime, &out->priority,
  //     &out->nice, &out->num_threads, &out->start_time, &out->vsize, &out->rss);
  int res = 1;
  // fprintf(stderr, "DEBUG: ************ read_proc_stat() line 77. OK here res=%d\n", res);
  return (res >= 20) ? 0 : -1;
#else
  return -1; // unimplemented
#endif
}

/// Read RSS of current process on Linux (multiplyed by page size)
long get_mem_rss(void) {
#ifdef __linux__
  FILE *f = fopen("/proc/self/statm", "r");
  if (!f)
    return -1;

  long size, resident, share;
  if (fscanf(f, "%ld %ld %ld", &size, &resident, &share) != 3) {
    fclose(f);
    return -1;
  }
  fclose(f);

  long page_size = sysconf(_SC_PAGESIZE);
  return resident * page_size; // bytes used now
#else
  return -1; // unimplemented
#endif
}

/// Get number of opened file descriptors on Linux
int get_open_fd_count(void) {
#ifdef __linux__
  DIR *dir = opendir("/proc/self/fd");
  if (!dir)
    return -1;

  int count = 0;
  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    if (ent->d_name[0] != '.') {
      // We can overcount symlinks here but that’s okay for “count”.
      count++;
    }
  }
  closedir(dir);
  return count;
#else
  return -1; // unimplemented
#endif
}

#if defined(__GLIBC__) &&                                                      \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 32))
#define HAS_GLIBC_2_32 1
#else
#define HAS_GLIBC_2_32 0
#endif

/**************
 * PARAMETERS *
 **************/
#define PROGRAM_NAME_LEN 32
#define BUFFER_SIZE 8192
#define ROOT_DIR "."
#define DEFAULT_PORT 8080
#define MAX_THREADS 16
#define MAX_THREADS_RETRY_COUNT 3
#define SLEEP_NSEC 10000

bool is_debug = false;
char command[PROGRAM_NAME_LEN + 1];

typedef struct {
  int client_fd;
} client_arg_t;

/* Global statistics structure */
typedef struct {
  int total_threads_started;
  int total_threads_finished;
  int thread_failures;
  unsigned long long total_request_handled;
  unsigned long long total_bytes_sent;
  double min_latency;
  double max_latency;
  pthread_mutex_t stats_mutex;
} Statistics;

Statistics stats = {0, 0, 0, 0, 0, 1000.0, 0, PTHREAD_MUTEX_INITIALIZER};

/* Atomic counter for active threads */
int active_threads = 0;
/// Mutex, fencing `active_threads`
pthread_mutex_t active_threads_mutex;

/// Increment active treads count, fenced with mutex
void increase_active_threads() {
  if(pthread_mutex_lock(&active_threads_mutex) == 0) {
    active_threads++; // Current active threads
    pthread_mutex_unlock(&active_threads_mutex);
  } else {
    fprintf(stderr, "ERROR LOCKING MUTEX: 'active_threads_mutex' while trying 'active_threads++'");
  }
}

/// Decrement active treads count, fenced with mutex
void decrease_active_threads() {
  if(pthread_mutex_lock(&active_threads_mutex) == 0) {
    active_threads--; // Current active threads
    pthread_mutex_unlock(&active_threads_mutex);
  } else {
    fprintf(stderr, "ERROR LOCKING MUTEX: 'active_threads_mutex' while trying 'active_threads--'");
  }
}

/* Flag shared between main and signal handler to stop on signal */
volatile sig_atomic_t keep_running = 1;

#if !HAS_GLIBC_2_32
const char *get_signal_name(int signum) {
  switch (signum) {
  case SIGINT:
    return "INT";
  case SIGQUIT:
    return "QUIT";
  case SIGTERM:
    return "TERM";
  default:
    return "UNKNOWN";
  }
}
#endif

/// Signal handler
void handle_signals(int signum) {
  if (signum == SIGINT || signum == SIGTERM) {
#if HAS_GLIBC_2_32
    const char *name = sigabbrev_np(signum);
#else
    const char *name = get_signal_name(signum);
#endif
    fprintf(stdout, "\nSignal SIG%s received, stopping new thread creation and "
           "exiting...\n",
           name);
    keep_running = 0;
  }
}

/// Measure time elapsed
double get_time_seconds() {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return now.tv_sec + now.tv_nsec * 1e-9;
}

/// Returns string that is consists from lowercased input string `s` chars
char *to_lower(const char *s) {
  size_t len = strlen(s) + 1; // +1 for '\0'
  char *res = malloc(len);
  for (int i = 0; i < len; i++) {
    res[i] = tolower((unsigned char)s[i]);
  }
  return res;
}

/// Print DEBUG string header
char *debug_hdr() {
  double time = get_time_seconds();
  size_t total_len = sizeof(time) + 12 + sizeof(command) +
                     1; // reserve size for formatted string "[DEBUG]: %s:
                        // %.12f:" -- 12 bytes of constant chars
  char *buf = malloc(total_len);
  if (!buf)
    return NULL;
  snprintf(buf, total_len, "[DEBUG]: %s: %.12f:", command, time);
  return buf;
}

/// Write statistics for handled HTTP request:
//
// `double start` -- thread starting time in seconds (from wall-clock)
//
// - increment total threads finished
// - increment total threads handled
// - update minimum or maximum latency if needed
void stats_handled(double start) {
  /* Update requests statistics on exit */
  double end = get_time_seconds();
  double elapsed = end - start;
  pthread_mutex_lock(&stats.stats_mutex);
  stats.max_latency = fmax(stats.max_latency, elapsed);
  stats.min_latency = fmin(stats.min_latency, elapsed);
  stats.total_threads_finished++;
  stats.total_request_handled++;
  pthread_mutex_unlock(&stats.stats_mutex);
}

/// Write statistics for unhandled HTTP request:
//
// `double start` -- thread starting time in seconds (from wall-clock)
//
// - increment total threads finished
// - update minimum or maximum latency if needed
void stats_unhandled(double start) {
  /* Update requests statistics on exit */
  double end = get_time_seconds();
  double elapsed = end - start;
  pthread_mutex_lock(&stats.stats_mutex);
  stats.max_latency = fmax(stats.max_latency, elapsed);
  stats.min_latency = fmin(stats.min_latency, elapsed);
  stats.total_threads_finished++;
  pthread_mutex_unlock(&stats.stats_mutex);
}

const char *get_mime_type(const char *path) {
  const char *ext = strrchr(path, '.');
  if (!ext)
    return "application/octet-stream";
  ext++;

  if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0)
    return "text/html";
  if (strcasecmp(ext, "txt") == 0)
    return "text/plain";
  if (strcasecmp(ext, "css") == 0)
    return "text/css";
  if (strcasecmp(ext, "js") == 0 || strcasecmp(ext, "ts") == 0)
    return "application/javascript";
  if (strcasecmp(ext, "json") == 0 || strcasecmp(ext, "yaml") == 0 ||
      strcasecmp(ext, "yml") == 0)
    return "application/json";
  if (strcasecmp(ext, "png") == 0)
    return "image/png";
  if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0)
    return "image/jpeg";
  if (strcasecmp(ext, "gif") == 0)
    return "image/gif";
  if (strcasecmp(ext, "svg") == 0)
    return "image/svg+xml";
  if (strcasecmp(ext, "ico") == 0)
    return "image/x-icon";
  if (strcasecmp(ext, "pdf") == 0)
    return "application/pdf";
  if (strcasecmp(ext, "xml") == 0 || strcasecmp(ext, "xhtml") == 0 ||
      strcasecmp(ext, "xhtm") == 0)
    return "application/xml";
  return "application/octet-stream";
}

/// HTTP response
void send_response(int client, int status, const char *status_text,
                   const char *mime, const char *body, size_t body_len) {
  char header[1024];
  int hdr_len = snprintf(header, sizeof(header),
                         "HTTP/1.1 %d %s\r\n"
                         "Content-Type: %s\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: close\r\n"
                         "\r\n",
                         status, status_text, mime, body_len);
  write(client, header, hdr_len);
  if (body_len > 0 && body) {
    write(client, body, body_len);
  }
  /* Update requests statistics */
  pthread_mutex_lock(&stats.stats_mutex);
  stats.total_bytes_sent += hdr_len;
  stats.total_bytes_sent += body_len;
  pthread_mutex_unlock(&stats.stats_mutex);
}

/// Respond with HTTP `503 Service Unavailable` error and
// `retry_after` seconds number
void send_response_503(int client, unsigned int retry_after) {
  // int status;
  char header[1024];
  char body[64];
  const char *status_text = "Service Unavailable";
  int body_len = snprintf(body, sizeof(body), "%s\r\n", status_text);
  const char *mime = "text/plain";
  int hdr_len = snprintf(header, sizeof(header),
                         "HTTP/1.1 %d %s\r\n"
                         "Content-Type: %s\r\n"
                         "Content-Length: %zu\r\n"
                         "Retry-After: %du\r\n"
                         "Connection: close\r\n"
                         "\r\n",
                         503, status_text, mime, body_len, retry_after);

  write(client, header, hdr_len);
  write(client, body, body_len);
  /* Update requests statistics */
  pthread_mutex_lock(&stats.stats_mutex);
  stats.total_bytes_sent += hdr_len;
  stats.total_bytes_sent += body_len;
  pthread_mutex_unlock(&stats.stats_mutex);
}

/// Response `503 Service Unavailable` status to client in new pthread
//
/// Count it as unhandled request
void *unhandle_client(void *arg) {

  double start = get_time_seconds();
  increase_active_threads();

  client_arg_t *carr = (client_arg_t *)arg;
  int client = carr->client_fd;
  free(carr);

  send_response_503(client, 10);
  close(client);
  stats_unhandled(start);
  if (is_debug) {
    char *dhdr = debug_hdr();
    pthread_t tid = pthread_self();
    fprintf(stderr, "%s %s%lu%s.\n", dhdr,
            "Finished request unhandled pthread tid=", (unsigned long)tid,
            " with HTTP status 503 Service Unavailable");
    free(dhdr);
  }
  decrease_active_threads();
  pthread_exit(NULL);
}

void *handle_client(void *arg) {
  double start = get_time_seconds();
  increase_active_threads();

  if (is_debug) {
    char *dhdr = debug_hdr();
    pthread_t tid = pthread_self();
    fprintf(stderr, "%s %s%lu.\n", dhdr,
            "Started new normal HTTP request handler pthread tid=",
            (unsigned long)tid);
    free(dhdr);
  }
  client_arg_t *carr = (client_arg_t *)arg;
  int client = carr->client_fd;
  free(carr);

  char buf[BUFFER_SIZE] = {0};
  ssize_t n = read(client, buf, sizeof(buf) - 1);
  if (n <= 0) {
    close(client);
    stats_unhandled(start);
    pthread_exit(NULL);
  }

  char method[16], path[512], proto[16];
  if (sscanf(buf, "%15s %511s %15s", method, path, proto) != 3 ||
      strcmp(method, "GET") != 0) {
    send_response(client, 400, "Bad Request", "text/plain", "Bad Request", 13);
    close(client);
    stats_handled(start);
    if (is_debug) {
      char *dhdr = debug_hdr();
      pthread_t tid = pthread_self();
      fprintf(stderr, "%s %s%lu%s.\n", dhdr,
              "Finished normal HTTP request handler pthread tid=",
              (unsigned long)tid, " with HTTP status 400 Bad Request");
      free(dhdr);
    }
    decrease_active_threads();
    pthread_exit(NULL);
  }

  char fullpath[1024];
  if (strcmp(path, "/") == 0)
    snprintf(fullpath, sizeof(fullpath), "%s/index.html", ROOT_DIR);
  else
    snprintf(fullpath, sizeof(fullpath), "%s%s", ROOT_DIR, path);

  if (strstr(path, "..")) {
    send_response(client, 403, "Forbidden", "text/plain", "Forbidden", 9);
    close(client);
    stats_handled(start);
    if (is_debug) {
      char *dhdr = debug_hdr();
      pthread_t tid = pthread_self();
      fprintf(stderr, "%s %s%lu%s.\n", dhdr,
              "Finished normal HTTP request handler pthread tid=",
              (unsigned long)tid, " with HTTP status 403 Forbidden");
      free(dhdr);
    }
    decrease_active_threads();
    pthread_exit(NULL);
  }

  struct stat st;
  if (stat(fullpath, &st) != 0 || S_ISDIR(st.st_mode)) {
    char idx[1024];
    snprintf(idx, sizeof(idx), "%s/index.html", fullpath);
    if (stat(idx, &st) == 0 && S_ISREG(st.st_mode))
      strncpy(fullpath, idx, sizeof(fullpath) - 1);
    else {
      send_response(client, 404, "Not Found", "text/plain", "404 Not Found",
                    13);
      close(client);
      stats_handled(start);
      if (is_debug) {
        char *dhdr = debug_hdr();
        pthread_t tid = pthread_self();
        fprintf(stderr, "%s %s%lu%s.\n", dhdr,
                "Finished normal HTTP request handler pthread tid=",
                (unsigned long)tid, " with HTTP status 404 Not Found");
        free(dhdr);
      }
      decrease_active_threads();
      pthread_exit(NULL);
    }
  }

  FILE *f = fopen(fullpath, "rb");
  if (!f) {
    send_response(client, 404, "Not Found", "text/plain", "404 Not Found", 13);
    close(client);
    stats_handled(start);
    if (is_debug) {
      char *dhdr = debug_hdr();
      pthread_t tid = pthread_self();
      fprintf(stderr, "%s %s%lu%s.\n", dhdr,
              "Finished normal HTTP request handler pthread tid=",
              (unsigned long)tid, " with HTTP status 404 Not Found");
      free(dhdr);
    }
    decrease_active_threads();
    pthread_exit(NULL);
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *body = malloc(size);
  if (!body) {
    fclose(f);
    close(client);
    stats_unhandled(start);
    if (is_debug) {
      char *dhdr = debug_hdr();
      pthread_t tid = pthread_self();
      fprintf(stderr, "%s %s%lu%s.\n", dhdr,
              "Finished normal HTTP request handler pthread tid=",
              (unsigned long)tid,
              " without response: cannot allocate memory for response body");
      free(dhdr);
    }
    pthread_exit(NULL);
  }

  fread(body, 1, size, f);
  fclose(f);

  const char *mime = get_mime_type(fullpath);
  send_response(client, 200, "OK", mime, body, (size_t)size);

  free(body);
  close(client);
  stats_handled(start);
  if (is_debug) {
    char *dhdr = debug_hdr();
    pthread_t tid = pthread_self();
    fprintf(stderr, "%s %s%lu%s.\n", dhdr,
            "Finished normal HTTP request handler pthread tid=",
            (unsigned long)tid, " with HTTP status 200 OK");
    free(dhdr);
  }
  decrease_active_threads();
  pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
  int port = DEFAULT_PORT;
  int start_time = (int)get_time_seconds();

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
  if (server_fd < 0) {
    perror("socket");
    exit(1);
  }

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    fprintf(stderr,
            "Failed to bind to port %d (may need root for ports < 1024)\n",
            port);
    exit(1);
  }
  if (listen(server_fd, 128) < 0) {
    perror("listen");
    exit(1);
  }

  struct sigaction sa;
  /* Setup signal handler using sigaction (safer than signal()) */
  sa.sa_handler = handle_signals;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0; /* No special flags */

  if ((sigaction(SIGINT, &sa, NULL) == -1) ||
      (sigaction(SIGTERM, &sa, NULL) == -1)) {
    perror("sigaction");
    exit(EXIT_FAILURE);
  }

  // Limit program name to maximum PROGRAM_NAME_LEN
  int argv_len = strlen(argv[0]);
  int command_name_len = argv_len < PROGRAM_NAME_LEN ? argv_len : PROGRAM_NAME_LEN;
  strncpy(command, argv[0], command_name_len);
  command[command_name_len] = '\0';
  const char *debugenv = getenv("DEBUG");
  if (debugenv != NULL) {
    const char *lower_env = to_lower(debugenv);
    if (strcmp(lower_env, "yes") || strcmp(lower_env, "1")) {
      is_debug = true;
      const char *dhdr = debug_hdr();
      fprintf(stderr, "%s Running server in DEBUG mode\n", dhdr);
    }
  }
  fprintf(stdout, "Multithreaded HTTP server listening on port %d\nPress Ctrl+C to stop "
         "safely...\n",
         port);

  unsigned long loop_count = 0L;
  while (keep_running) {
    if (is_debug) {
      char *dhdr = debug_hdr();
      fprintf(stderr, "%s %s%lu.\n", dhdr,
              "In main loop count=", loop_count);
      proc_stat *st;
      int cpu_utime;
      int cpu_stime;
      if (read_proc_stat(st) == 0) {
        cpu_utime = st->utime;
        cpu_stime = st->stime;
      } else {
        cpu_utime = cpu_stime = 0;
      }
      fprintf(stderr, "%s %s%lu%s%d%s%d%s%d%s%s%d%s.\n", dhdr,
              "Process stats: [RSS=", get_mem_rss()/1024, " Kbytes] [CPU_system=", cpu_stime, "] [CPU_user=", cpu_utime,
              "], [opened_file_descriptors=", get_open_fd_count(), "]", " [active_threads=", active_threads, "]");
      free(dhdr);
    }
    loop_count++;
    int client = accept(server_fd, (struct sockaddr *)&addr, &addr_len);
    if (client < 0)
      continue;

    client_arg_t *carr = malloc(sizeof(client_arg_t));
    if (!carr) {
      close(client);
      continue;
    }
    carr->client_fd = client;

    /* Do not overrun MAX_THREADS pthreads
     * Retry `retry` times and the return server error (HTTP 500)
     */
    int retry = MAX_THREADS_RETRY_COUNT;
    while ((active_threads >= MAX_THREADS) && (retry > 0)) {
      if (is_debug) {
        char *dhdr = debug_hdr();
        fprintf(stderr, "%s %s%d%s%d%s%d.\n", dhdr,
                "Waiting for active_threads=", active_threads,
                " to become lower then MAX_THREADS=", MAX_THREADS, " retry ",
                retry);
        free(dhdr);
      }
      /* Wait a bit before retrying */
      retry--;
      usleep(SLEEP_NSEC);
    }
    /* Return server error because of overload and continue*/
    if (retry <= 0) {
      pthread_t etid;
      if (pthread_create(&etid, NULL, unhandle_client, carr) != 0) {
        perror("pthread_create");
        free(carr);
        close(client);
        pthread_mutex_lock(&stats.stats_mutex);
        stats.thread_failures++;
        pthread_mutex_unlock(&stats.stats_mutex);
        continue;
      }
      if (is_debug) {
        char *dhdr = debug_hdr();
        fprintf(stderr, "%s %s%d%s.\n", dhdr, "Sending 503 HTTP Error because ",
                active_threads, " threads are still running");
        fprintf(stderr, "%s %s%lu.\n", dhdr,
                "Spawning HTTP error handler in tid=", (unsigned long)etid);
        free(dhdr);
      }
      /* Update started count */
      pthread_mutex_lock(&stats.stats_mutex);
      stats.total_threads_started++;
      pthread_mutex_unlock(&stats.stats_mutex);
      pthread_detach(etid);
      continue;
    }

    pthread_t tid;
    if (pthread_create(&tid, NULL, handle_client, carr) != 0) {
      perror("pthread_create");
      free(carr);
      close(client);
      pthread_mutex_lock(&stats.stats_mutex);
      stats.thread_failures++;
      pthread_mutex_unlock(&stats.stats_mutex);
      continue;
    }
    if (is_debug) {
      char *dhdr = debug_hdr();
      fprintf(stderr, "%s %s%lu.\n", dhdr,
              "Spawning normal request handler in pthread tid=",
              (unsigned long)tid);
      free(dhdr);
    }
    /* Update started count */
    pthread_mutex_lock(&stats.stats_mutex);
    stats.total_threads_started++;
    pthread_mutex_unlock(&stats.stats_mutex);
    pthread_detach(tid);
  }

  /* Wait while threads are still active */
  while (active_threads > 0) {
    fprintf(stdout, "\nWaiting for remaining %d threads to finish...\n", active_threads);
    usleep(10000);
  }

  int end_time = (int)get_time_seconds();
  int uptime = end_time - start_time;
  /* Print final statistics */
  fprintf(stdout, "\n======== Final Statistics ========\n");
  fprintf(stdout, "Total threads started:        %d\n", stats.total_threads_started);
  fprintf(stdout, "Total threads finished:       %d\n", stats.total_threads_finished);
  fprintf(stdout, "Thread failures:              %d\n", stats.thread_failures);
  fprintf(stdout, "Total HTTP request processed: %llu\n", stats.total_request_handled);
  fprintf(stdout, "Total bytes sent:             %llu\n", stats.total_bytes_sent);
  fprintf(stdout, "Minimum request latency, ms:  %.6f\n",
         (stats.min_latency == 1000) ? 0 : stats.min_latency * 1000);
  fprintf(stdout, "Maximum request latency, ms:  %.6f\n", stats.max_latency * 1000);
  fprintf(stdout, "Server uptime, s:             %d\n", uptime);
  fprintf(stdout, "==================================\n");

  /* Cleanup mutexes */
  pthread_mutex_destroy(&stats.stats_mutex);
  pthread_mutex_destroy(&active_threads_mutex);

  close(server_fd);
  return 0;
}
