# 3. A real background payload

`hello.elf` ran and exited. Most useful payloads keep running: they watch
something, serve a page, or keep a setting applied. That brings questions
hello.elf never had to answer: what if it's loaded twice, how do you see what
it's doing, how does it stop, and what happens in rest mode?

[`payload/service/service.c`](../payload/service/service.c) answers all of them
in about 300 lines. It serves a status page you can open on your phone. This
chapter goes through it part by part.

## Build and run it

```bash
make -C payload/service
```

Load `payload/service/service.elf` like in chapter 2. A notification says
**service is running** with an address. Open it on your phone or PC:

```
http://192.168.0.10:8080
```

```
service 1.0 is running.
Up 12 s, CPU 52 C, 1 requests.
JSON: /status
```

`/status` gives the same as JSON, for programs (the app in chapter 9 reads it):

```json
{"name":"service","version":"1.0","uptime":12,"cpu":52,"requests":2}
```

## One copy at a time

Load it twice and you'd have two copies fighting over port 8080. Worse, for a
payload that changes a setting, two copies would undo each other's work.
`take_lock()` makes sure there's only ever one:

```c
int fd = open(LOCK_PATH, O_RDWR | O_CREAT, 0666);
if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
    /* someone holds it: read their pid, ask them to stop, wait for the lock */
}
/* write our pid into the file, and keep fd open */
```

`flock` locks belong to a process. However the process ends, crash included,
the system releases its lock, so it can never be left stuck.

The new copy doesn't give up when the lock is taken. It reads the old copy's
pid from the file, sends it `SIGTERM`, and waits for the lock. So **loading a
new version replaces the old one by itself**, which is exactly what you want
while developing, and later for updates.

## A log you can read

```c
s_log = fopen(LOG_PATH, "a");
...
fprintf(s_log, "[%s] ", stamp);
...
fflush(s_log);
```

The log is `/data/ps5-starter/service.log`. Read it over FTP any time:

```bash
curl ftp://192.168.0.10:2121/data/ps5-starter/service.log
```

```
[2026-10-20 14:02:11] service 1.0 starting (pid 115)
[2026-10-20 14:02:11] web: listening on port 8080
[2026-10-20 14:02:11] notify: service is running http://192.168.0.10:8080
```

`fflush` after every line matters: if the payload crashes or is killed, the
last lines are already on disk, and those are the ones you need.

## Signals: stopping cleanly

```c
static void on_stop(int sig) { s_running = 0; }
...
signal(SIGTERM, on_stop);
while (s_running) { ... }
/* cleanup */
```

A signal handler can interrupt your code anywhere, so it should only set a
flag. The main loop checks the flag and does the actual cleanup: close the
socket, close the log, release the lock.

**`SIGKILL` can't be caught.** Payload Manager's "kill" sends `SIGKILL`: the
process just ends, and your cleanup never runs. If your payload changes
something that must be put back, like a system setting, plan for that.
Breeze, for example, starts a small second payload whose only job is to put
the fan back if the main one disappears.

## The status page

The main loop doubles as the web server:

```c
struct pollfd p = {.fd = server, .events = POLLIN};
int ready = poll(&p, 1, 1000);   /* wait up to 1 s for a visitor */
if (ready > 0) { accept, answer, close }
/* then the once-a-second work */
```

`poll` waits until a browser connects or a second passes, whichever comes
first. So one thread serves the page and also does the periodic work, with no
locking to think about. For a status page that's plenty. A busier server
would use a thread.

Two lines that save you a crash:

```c
signal(SIGPIPE, SIG_IGN);
setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
```

When a browser hangs up while you're still sending, the system sends
`SIGPIPE`, and by default that **ends your process**. These lines turn it into
an ordinary error from `send()`.

## Small things that help

- `sceKernelSetProcessName("service.elf")`: loaders name every payload
  `payload.elf`. With a name, you can tell yours apart in process lists.
- `__attribute__((weak))` on system functions you can live without: the
  payload still loads on a firmware that lacks one.
- Everything the payload keeps goes in its own folder, `/data/ps5-starter/`.
  Use your own name for yours.

## Try this

- Load it again while it's running, and watch the log: *replacing the running
  copy*, then a new start.
- Add a field to `/status`, like the time since the last request.
- Stop it with Payload Manager's process list, and notice the log has no
  *stopping* line: that's `SIGKILL`.

One part of the main loop this chapter skipped: the address checks and
`SIGCONT`. Without them, the page stops opening after the console has been in
rest mode, although the payload is still running. That's next.

Next: [Surviving rest mode](04-surviving-rest-mode.md)
