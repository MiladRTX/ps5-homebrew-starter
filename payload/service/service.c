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
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "service_version.h"

#define NAME SERVICE_NAME /* in service_version.h, which the app shares */
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

/* The format attribute lets the compiler check the arguments, like printf's. */
static void log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void notify(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Opens the log. Over 1 MB, the old one is kept as service.log.old and a new
 * one starts, so the log can't fill up /data over the months. */
static void open_log(void) {
    struct stat st;
    if (stat(LOG_PATH, &st) == 0 && st.st_size > 1024 * 1024)
        rename(LOG_PATH, LOG_PATH ".old");
    s_log = fopen(LOG_PATH, "a");
}

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

/* Like signal(), but a signal also interrupts a wait in progress (recv,
 * poll), so a stop request is noticed at once. */
static void catch_signal(int sig, void (*handler)(int)) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

/* ---- One copy at a time ------------------------------------------------
 * The lock file holds the running copy's pid. flock() locks are released by
 * the system when their process ends, however it ends, so a lock is never
 * left behind by a crash. A new copy asks the old one to stop (SIGTERM),
 * waits for the lock, and takes over: launching a new version replaces the
 * old one. */
#define LOCK_CANT_OPEN -2 /* take_lock(): the file couldn't be opened at all */

static int take_lock(void) {
    int fd = open(LOCK_PATH, O_RDWR | O_CREAT, 0666);
    if (fd < 0)
        return LOCK_CANT_OPEN;
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

static int s_last_error; /* the last reason the port didn't open: logged once */

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
        if (errno != s_last_error) /* not the same line every 10 s */
            log_line("web: port %d: %s", PORT, strerror(errno));
        s_last_error = errno;
        close(fd);
        return -1;
    }
    s_last_error = 0;
    log_line("web: listening on port %d", PORT);
    return fd;
}

/* ---- The status page ------------------------------------------------------ */
static time_t s_started;
static unsigned s_requests;

/* Does the request line ask for this page? "GET /status HTTP/1.1" asks for
 * "/status", "GET /status?x=1 ..." too, "GET /statusfoo ..." doesn't. */
static int asks_for(const char *request, const char *page) {
    size_t n = strlen(page);
    return !strncmp(request, "GET ", 4) && !strncmp(request + 4, page, n) &&
           (request[4 + n] == ' ' || request[4 + n] == '?');
}

static void answer(int client) {
    /* The request can arrive in pieces: read until the end of its headers
     * (a blank line), or until the buffer is full. */
    char request[1024];
    size_t got = 0;
    while (got < sizeof request - 1) {
        ssize_t n = recv(client, request + got, sizeof request - 1 - got, 0);
        if (n <= 0)
            break; /* closed, or nothing within the time limit (main()) */
        got += (size_t)n;
        request[got] = 0;
        if (strstr(request, "\r\n\r\n"))
            break;
    }
    if (got == 0)
        return;
    request[got] = 0;

    char head[160];
    int hlen;
    const int json = asks_for(request, "/status");
    if (!json && !asks_for(request, "/")) {
        /* Anything else, like the /favicon.ico browsers ask for: not found. */
        hlen = snprintf(head, sizeof head,
                        "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        send(client, head, (size_t)hlen, 0);
        return;
    }
    s_requests++;

    int cpu = -1;
    if (sceKernelGetCpuTemperature)
        sceKernelGetCpuTemperature(&cpu);

    char body[512];
    int len;
    if (json)
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
    /* snprintf returns the length the text would have had: a page that grew
     * past body[] is cut, never sent with bytes from beyond it. */
    if (len < 0)
        len = 0;
    if (len >= (int)sizeof body) {
        log_line("web: page cut to %d bytes: make body[] bigger", (int)sizeof body - 1);
        len = (int)sizeof body - 1;
    }

    hlen = snprintf(head, sizeof head,
                    "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
                    "Connection: close\r\n\r\n",
                    json ? "application/json" : "text/plain", len);
    if (hlen < 0 || hlen >= (int)sizeof head)
        return;
    send(client, head, (size_t)hlen, 0);
    send(client, body, (size_t)len, 0);
}

/* ---- Main ------------------------------------------------------------------ */
int main(void) {
    catch_signal(SIGTERM, on_stop);
    catch_signal(SIGINT, on_stop);
    catch_signal(SIGCONT, on_resume);
    signal(SIGPIPE, SIG_IGN);
    if (sceKernelSetProcessName) /* loaders name every payload "payload.elf" */
        sceKernelSetProcessName(NAME ".elf");

    mkdir(DATA_DIR, 0777);
    open_log();
    int lock = take_lock();
    if (lock == LOCK_CANT_OPEN) {
        notify("%s: can't open %s: %s", NAME, LOCK_PATH, strerror(errno));
        return 1;
    }
    if (lock < 0) {
        notify("%s: another copy is running and would not stop", NAME);
        return 1;
    }
    s_started = time(NULL);
    log_line("%s %s starting (pid %d)", NAME, SERVICE_VERSION, getpid());

    char addr[INET_ADDRSTRLEN];
    char shown[INET_ADDRSTRLEN] = ""; /* the address the user last saw */
    local_address(addr, sizeof addr);
    int server = open_server();
    if (server >= 0 && addr[0]) {
        notify("%s is running\nhttp://%s:%d", NAME, addr, PORT);
        memcpy(shown, addr, sizeof shown);
    }
    else if (server < 0 && addr[0])
        notify("%s is running, but port %d is busy (see service.log)", NAME, PORT);
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
                /* One thread does everything, so every visitor gets 1 s: one
                 * that connects and says nothing (a browser's spare
                 * connection, a scanner) can't hold up the service. */
                struct timeval limit = {1, 0};
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof limit);
                setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof limit);
                answer(client);
                /* Say we're done, then read what's left of the request: closing
                 * with unread bytes would reset the connection, and some
                 * browsers then drop the page. */
                shutdown(client, SHUT_WR);
                char rest[256];
                for (int i = 0; i < 16 && recv(client, rest, sizeof rest, 0) > 0; i++) {
                }
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
                /* A new address (another network, a new lease after rest
                 * mode): say where the page is now. The same one: quiet. */
                if (server >= 0 && strcmp(shown, addr) != 0) {
                    notify("%s is running\nhttp://%s:%d", NAME, addr, PORT);
                    memcpy(shown, addr, sizeof shown);
                }
            }
        }
    }

    log_line("%s stopping", NAME);
    if (server >= 0)
        close(server);
    /* An empty lock file: no pid left behind for anything to act on. (A crash
     * still leaves one; the lock itself is what counts.) */
    if (ftruncate(lock, 0) != 0)
        log_line("can't empty %s: %s", LOCK_PATH, strerror(errno));
    if (s_log)
        fclose(s_log);
    close(lock); /* releases the lock: a waiting new copy takes over now */
    return 0;
}
