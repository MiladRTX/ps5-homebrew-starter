/*
 * PS5 Homebrew Starter - talking to a background payload on the same console.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "link.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#ifdef STARTER_SIM
#include <signal.h>
#include <spawn.h>
extern char **environ;
#endif

namespace starter::link
{
namespace
{
[[maybe_unused]] constexpr int loader_port = 9021;

// A connection to 127.0.0.1:port, -1 when nothing accepts it. timeout_s
// bounds every send and receive, so a stuck payload can't freeze the app.
int connect_local(int port, int timeout_s) noexcept
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    timeval tv{timeout_s, 0};
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#ifdef SO_NOSIGPIPE
    const int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    sockaddr_in a{};
#ifndef STARTER_PREVIEW // BSD has sin_len; the PC preview builds on Linux
    a.sin_len = sizeof a;
#endif
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<unsigned short>(port));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<const sockaddr *>(&a), sizeof a) != 0)
    {
        (void)close(fd);
        return -1;
    }
    return fd;
}

bool send_all(int fd, const void *data, size_t len) noexcept
{
    const auto *p = static_cast<const char *>(data);
    while (len > 0)
    {
        const ssize_t n = send(fd, p, len, 0);
        if (n <= 0)
            return false;
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}
} // namespace

int http_get(int port, const char *path, char *out, std::size_t cap) noexcept
{
    out[0] = '\0';
    const int fd = connect_local(port, 1);
    if (fd < 0)
        return 0;
    char reply[4096];
    char head[256];
    const int hlen = snprintf(head, sizeof head,
                              "GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n", path);
    size_t len = 0;
    if (hlen > 0 && send_all(fd, head, static_cast<size_t>(hlen)))
    {
        while (len < sizeof reply - 1)
        {
            const ssize_t n = recv(fd, reply + len, sizeof reply - 1 - len, 0);
            if (n <= 0)
                break;
            len += static_cast<size_t>(n);
        }
    }
    (void)close(fd);
    reply[len] = '\0';

    // "HTTP/1.1 200 OK\r\n...\r\n\r\n<body>"
    if (strncmp(reply, "HTTP/1.", 7) != 0 || len < 12)
        return -1;
    const int status = atoi(reply + 9);
    const char *body = strstr(reply, "\r\n\r\n");
    if (body)
        snprintf(out, cap, "%s", body + 4);
    return status;
}

#ifdef STARTER_SIM
// The PC simulator (app/sim): no ELF loader on a PC. The service is built as
// a PC program next to where the PS5 app has service.elf, and started as a
// process of its own, so it keeps running after the simulator closes, like on
// the PS5. Everything else, asking it and replacing it, works as on the PS5.
int start_payload(const char *path) noexcept
{
    char program[512];
    snprintf(program, sizeof program, "%s", path);
    const std::size_t n = strlen(program);
    if (n > 4 && !strcmp(program + n - 4, ".elf"))
        program[n - 4] = '\0';
    if (access(program, X_OK) != 0)
        return -1;
    signal(SIGCHLD, SIG_IGN); // nobody waits for it: let the system clean up after it
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID); // its own session, like a payload
    // Not attached to the simulator's terminal: it outlives it, and writes
    // everything to its own log in data/ps5-starter anyway.
    posix_spawn_file_actions_t files;
    posix_spawn_file_actions_init(&files);
    posix_spawn_file_actions_addopen(&files, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&files, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_adddup2(&files, 1, 2);
    char *argv[] = {program, nullptr};
    pid_t pid = 0;
    const int r = posix_spawn(&pid, program, &files, &attr, argv, environ);
    posix_spawn_file_actions_destroy(&files);
    posix_spawnattr_destroy(&attr);
    return r == 0 ? 0 : -2;
}
#else
int start_payload(const char *path) noexcept
{
    static unsigned char elf[1 << 20]; // 1 MB is plenty for a payload
    const int file = open(path, O_RDONLY);
    if (file < 0)
        return -1;
    size_t len = 0;
    for (;;)
    {
        const ssize_t n = read(file, elf + len, sizeof elf - len);
        if (n <= 0)
            break;
        len += static_cast<size_t>(n);
        if (len == sizeof elf)
            break;
    }
    (void)close(file);
    if (len < 64 || len == sizeof elf || memcmp(elf, "\x7f" "ELF", 4) != 0)
        return -1;

    // The loader starts whatever ELF arrives on its port, then the
    // connection closes. Nothing else to it.
    const int s = connect_local(loader_port, 5);
    if (s < 0)
        return -2;
    const bool sent = send_all(s, elf, len);
    (void)close(s);
    return sent ? 0 : -2;
}
#endif

bool console_address(char *out, std::size_t cap) noexcept
{
#ifdef STARTER_SIM
    snprintf(out, cap, "localhost");
    return true;
#else
    // A UDP "connection" sends nothing; it only makes the system pick the
    // network the console reaches the internet on, and so its address there.
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return false;
    sockaddr_in a{};
#ifndef STARTER_PREVIEW // BSD has sin_len; the PC builds are Linux
    a.sin_len = sizeof a;
#endif
    a.sin_family = AF_INET;
    a.sin_port = htons(53);
    a.sin_addr.s_addr = htonl(0x01010101); // 1.1.1.1
    sockaddr_in me{};
    socklen_t len = sizeof me;
    const bool ok = connect(fd, reinterpret_cast<const sockaddr *>(&a), sizeof a) == 0 &&
                    getsockname(fd, reinterpret_cast<sockaddr *>(&me), &len) == 0 &&
                    me.sin_addr.s_addr != 0;
    (void)close(fd);
    if (!ok)
        return false;
    const unsigned ip = ntohl(me.sin_addr.s_addr);
    snprintf(out, cap, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    return true;
#endif
}

long json_number(const char *json, const char *key, long fallback) noexcept
{
    char pattern[64];
    snprintf(pattern, sizeof pattern, "\"%s\":", key);
    const char *at = strstr(json, pattern);
    if (!at)
        return fallback;
    char *end = nullptr;
    const long value = strtol(at + strlen(pattern), &end, 10);
    return end == at + strlen(pattern) ? fallback : value;
}

void json_text(const char *json, const char *key, char *out, std::size_t cap) noexcept
{
    out[0] = '\0';
    char pattern[64];
    snprintf(pattern, sizeof pattern, "\"%s\":\"", key);
    const char *at = strstr(json, pattern);
    if (!at)
        return;
    at += strlen(pattern);
    std::size_t i = 0;
    while (at[i] && at[i] != '"' && i + 1 < cap)
    {
        out[i] = at[i];
        ++i;
    }
    out[i] = '\0';
}

int compare_versions(const char *a, const char *b) noexcept
{
    while (*a || *b)
    {
        // The next number of each; a missing one counts as 0.
        const long x = strtol(a, const_cast<char **>(&a), 10);
        const long y = strtol(b, const_cast<char **>(&b), 10);
        if (x != y)
            return x < y ? -1 : 1;
        // Past the dot to the next number; anything else ends the comparison.
        if (*a != '.' && *b != '.')
            break;
        if (*a == '.')
            ++a;
        if (*b == '.')
            ++b;
    }
    return 0;
}
} // namespace starter::link
