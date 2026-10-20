/* A background payload that keeps running: the shape of a real homebrew
 * service (Breeze is built this way). It
 *   - runs as one copy only, and a newer copy replaces an older one,
 *   - logs to a file in /data,
 *   - serves a status page on port 8080 (open http://<PS5 IP>:8080 on a phone),
 *   - survives rest mode: the system closes listening sockets on the way
 *     into rest mode, so the page is reopened after waking up,
 *   - stops cleanly on SIGTERM.
 * docs/03-background-payload.md and docs/04-surviving-rest-mode.md walk
 * through it. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "service_version.h"

#define NAME "service"
#ifndef DATA_DIR /* the PC simulator's build sets its own (app/sim/run.sh) */
#define DATA_DIR "/data/ps5-starter"
#endif
#define LOG_PATH DATA_DIR "/service.log"
#define LOCK_PATH DATA_DIR "/service.lock"
#define PORT 8080

/* ---- System functions ----------------------------------------------------
 * Declared here, provided by the console's libraries. weak: if a firmware
 * lacks one, its address is NULL instead of the payload failing to load. */
typedef struct notify_request {
    char reserved[45];
    char message[3075];
} notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
int sceKernelGetCpuTemperature(int *celsius) __attribute__((weak));
int sceKernelSetProcessName(const char *name) __attribute__((weak));

/* ---- Log -------------------------------------------------------------- */
static FILE *s_log;

static void log_line(const char *fmt, ...) {
    if (!s_log)
        return;
    time_t now = time(NULL);
    char stamp[32];
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(s_log, "[%s] ", stamp);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(s_log, fmt, ap);
    va_end(ap);
    fputc('\n', s_log);
    fflush(s_log); /* a crash or a hard kill must not lose the last lines */
}

static void notify(const char *fmt, ...) {
    notify_request_t request;
    memset(&request, 0, sizeof request);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(request.message, sizeof request.message, fmt, ap);
    va_end(ap);
    sceKernelSendNotificationRequest(0, &request, sizeof request, 0);
    for (char *p = request.message; *p; p++) /* one log line per entry */
        if (*p == '\n')
            *p = ' ';
    log_line("notify: %s", request.message);
}

/* ---- Signals -----------------------------------------------------------
 * A handler may only set a flag; the main loop does the actual work. */
static volatile sig_atomic_t s_running = 1;
static volatile sig_atomic_t s_resumed;

static void on_stop(int sig) {
    (void)sig;
    s_running = 0;
}

static void on_resume(int sig) { /* SIGCONT: sent to processes on waking up */
    (void)sig;
    s_resumed = 1;
}

/* ---- One copy at a time ------------------------------------------------
 * The lock file holds the running copy's pid. flock() locks are released by
 * the system when their process ends, however it ends, so a lock is never
 * left behind by a crash. A new copy asks the old one to stop (SIGTERM),
 * waits for the lock, and takes over: launching a new version replaces the
 * old one. */
static int take_lock(void) {
    int fd = open(LOCK_PATH, O_RDWR | O_CREAT, 0666);
    if (fd < 0)
        return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        char buf[16] = {0};
        int pid = pread(fd, buf, sizeof buf - 1, 0) > 0 ? atoi(buf) : 0;
        if (pid > 1 && pid != getpid()) {
            log_line("replacing the running copy (pid %d)", pid);
            kill(pid, SIGTERM);
        }
        int waited = 0;
        while (flock(fd, LOCK_EX | LOCK_NB) != 0) {
            if (waited >= 5000) { /* 5 s: it didn't stop */
                close(fd);
                return -1;
            }
            usleep(100 * 1000);
            waited += 100;
        }
    }
    char buf[16];
    int n = snprintf(buf, sizeof buf, "%d\n", getpid());
    if (ftruncate(fd, 0) == 0)
        (void)pwrite(fd, buf, (size_t)n, 0);
    return fd; /* kept open: closing it would release the lock */
}

/* ---- Network -------------------------------------------------------------- */

/* The console's IPv4 address, "" if it has none (no network, or in rest mode). */
static void local_address(char *out, size_t n) {
    out[0] = 0;
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) != 0)
        return;
    for (struct ifaddrs *i = list; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) ||
            (i->ifa_flags & IFF_LOOPBACK))
            continue;
        struct in_addr a = ((struct sockaddr_in *)i->ifa_addr)->sin_addr;
        if (a.s_addr == 0) /* 0.0.0.0: waking up, the network has no address yet */
            continue;
        inet_ntop(AF_INET, &a, out, (socklen_t)n);
        break;
    }
    freeifaddrs(list);
}

static int open_server(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    /* Without this, a browser that hangs up mid-answer would end the
     * process with SIGPIPE. (BSD only: on Linux, main() ignoring SIGPIPE
     * does the job.) */
#ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(fd, 8) != 0) {
        log_line("web: port %d: %s", PORT, strerror(errno));
        close(fd);
        return -1;
    }
    log_line("web: listening on port %d", PORT);
    return fd;
}

/* ---- The status page ------------------------------------------------------ */
static time_t s_started;
static unsigned s_requests;

static void answer(int client) {
    char request[1024];
    ssize_t got = recv(client, request, sizeof request - 1, 0);
    if (got <= 0)
        return;
    request[got] = 0;
    s_requests++;

    int cpu = -1;
    if (sceKernelGetCpuTemperature)
        sceKernelGetCpuTemperature(&cpu);

    char body[256];
    int len;
    if (!strncmp(request, "GET /status", 11))
        len = snprintf(body, sizeof body,
                       "{\"name\":\"%s\",\"version\":\"%s\",\"uptime\":%ld,\"cpu\":%d,"
                       "\"requests\":%u}\n",
                       NAME, SERVICE_VERSION, (long)(time(NULL) - s_started), cpu, s_requests);
    else {
        char cpu_text[16];
        if (cpu > 0)
            snprintf(cpu_text, sizeof cpu_text, "%d C", cpu);
        else
            snprintf(cpu_text, sizeof cpu_text, "unknown");
        len = snprintf(body, sizeof body,
                       "%s %s is running.\nUp %ld s, CPU %s, %u requests.\n"
                       "JSON: /status\n",
                       NAME, SERVICE_VERSION, (long)(time(NULL) - s_started), cpu_text, s_requests);
    }
    const char *type = !strncmp(request, "GET /status", 11) ? "application/json" : "text/plain";

    char head[160];
    int hlen = snprintf(head, sizeof head,
                        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
                        "Connection: close\r\n\r\n",
                        type, len);
    send(client, head, (size_t)hlen, 0);
    send(client, body, (size_t)len, 0);
}

/* ---- Main ------------------------------------------------------------------ */
int main(void) {
    signal(SIGTERM, on_stop);
    signal(SIGINT, on_stop);
    signal(SIGCONT, on_resume);
    signal(SIGPIPE, SIG_IGN);
    if (sceKernelSetProcessName) /* loaders name every payload "payload.elf" */
        sceKernelSetProcessName(NAME ".elf");

    mkdir(DATA_DIR, 0777);
    s_log = fopen(LOG_PATH, "a");
    int lock = take_lock();
    if (lock < 0) {
        notify("%s: another copy is running and would not stop", NAME);
        return 1;
    }
    s_started = time(NULL);
    log_line("%s %s starting (pid %d)", NAME, SERVICE_VERSION, getpid());

    char addr[INET_ADDRSTRLEN];
    local_address(addr, sizeof addr);
    int server = open_server();
    if (server >= 0 && addr[0])
        notify("%s is running\nhttp://%s:%d", NAME, addr, PORT);
    else
        notify("%s is running", NAME);

    time_t reopen_at = 0;    /* when to open the page again, 0: not planned yet */
    time_t last_check = 0;
    while (s_running) {
        /* Wait up to 1 s for a visitor; this is also the loop's tick. */
        struct pollfd p = {.fd = server, .events = POLLIN};
        int ready = server >= 0 ? poll(&p, 1, 1000) : (sleep(1), 0);
        if (!s_running)
            break;
        if (ready > 0 && (p.revents & POLLIN)) {
            int client = accept(server, NULL, NULL);
            if (client >= 0) {
                answer(client);
                close(client);
            }
        }

        /* Rest mode: the listening socket is dead after waking up, although
         * this process kept running. Two signs of it, either is enough:
         * SIGCONT, and the console's address changing (it goes away on the
         * way into rest mode and comes back on waking). */
        time_t now = time(NULL);
        int woke = s_resumed;
        s_resumed = 0;
        if (now != last_check) { /* the address, once a second */
            last_check = now;
            char was[INET_ADDRSTRLEN];
            memcpy(was, addr, sizeof was);
            local_address(addr, sizeof addr);
            if (strcmp(was, addr) != 0) {
                log_line("net: address '%s' -> '%s'", was, addr);
                woke = 1;
            }
        }
        if (woke && server >= 0) {
            log_line("web: closing the page until the network is back");
            close(server);
            server = -1;
            reopen_at = 0;
        }
        /* (Re)open once there's an address again, 2 s after it came: the
         * network needs a moment after waking. If opening fails (the port is
         * busy), try again every 10 s. */
        if (server < 0 && addr[0]) {
            if (reopen_at == 0)
                reopen_at = now + 2;
            else if (now >= reopen_at) {
                server = open_server();
                reopen_at = server >= 0 ? 0 : now + 10;
            }
        }
    }

    log_line("%s stopping", NAME);
    if (server >= 0)
        close(server);
    if (s_log)
        fclose(s_log);
    close(lock); /* releases the lock: a waiting new copy takes over now */
    return 0;
}
