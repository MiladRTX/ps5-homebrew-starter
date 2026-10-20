# 4. Surviving rest mode

Rest mode is where background payloads quietly break. Everything looks fine
until someone wakes the console and your page, or your server, doesn't answer.
This chapter explains what happens and how `service.c` handles it. These are
the same fixes Breeze uses, tested on a real console.

## What rest mode does to a payload

- **Your process keeps existing.** It's still there after waking, with its
  memory, open files and variables as they were.
- **Every listening socket is closed by the system.** Your server socket is
  still open as far as your code knows: no error, nothing. But connections to
  that port are refused. The port is dead until you open a new socket.
- **The network goes away and comes back.** The console's IP address
  disappears on the way into rest mode and returns after waking, sometimes as a
  different address if the router hands out a new one.
- **Time jumps.** After waking, `time()` is however much later it really is.

It's not only your payload. Homebrew servers that don't handle this stop
answering after rest mode too. In testing, an FTP server and an ELF loader's
port both needed restarting, while Payload Manager kept working because it
reopens its server. Keep that in mind when your payload, or your app,
depends on one of them.

## Noticing that the console woke up

There are two signs, and `service.c` watches both, because either can be the
one that arrives:

**The address changing.** Once a second, the main loop reads the console's
IPv4 address and compares it with the last one:

```c
local_address(addr, sizeof addr);
if (strcmp(was, addr) != 0) {
    log_line("net: address '%s' -> '%s'", was, addr);
    woke = 1;
}
```

Going into rest mode the address becomes empty; on waking it comes back. Both
changes trigger it, and a new address from the router does too, which needs
the same fix anyway. While waking, a PS5 briefly reports `0.0.0.0` (the
network is on, but has no address yet); `local_address()` skips that, so the
page waits for the real address.

**`SIGCONT`.** Processes get it when the console resumes. As with any signal,
the handler only sets a flag:

```c
static void on_resume(int sig) { s_resumed = 1; }
...
signal(SIGCONT, on_resume);
```

In Breeze's testing on a PS5 Slim, the address check is what caught it every
time. Payload Manager uses `SIGCONT`. Watching both costs nothing.

## Reopening the server

Once it knows, the loop closes the dead socket, and opens a fresh one two
seconds after the console has an address again:

```c
if (woke && server >= 0) {
    log_line("web: closing the page until the network is back");
    close(server);
    server = -1;
    reopen_at = 0;
}
if (server < 0 && addr[0]) {            /* there's a network again */
    if (reopen_at == 0)
        reopen_at = now + 2;            /* give it a moment */
    else if (now >= reopen_at) {
        server = open_server();
        reopen_at = server >= 0 ? 0 : now + 10;   /* failed: retry in 10 s */
    }
}
```

Why wait for the address: a socket opened while there's no network is just as
dead after waking. Why two more seconds: right after the address comes back,
the network isn't fully up yet. Two seconds was enough in testing. And if
opening fails anyway (the port is still taken, say), the loop tries again
every 10 seconds instead of giving up.

`SO_REUSEADDR` in `open_server()` matters here too: without it, binding the
same port again shortly after closing it can fail.

## What else to plan for

- **Connections that were open are gone.** A client that was connected before
  rest mode gets an error. Close and reconnect; don't keep long-lived
  connections without a way to recover.
- **Outgoing requests fail for a moment after waking.** Retry later instead of
  giving up for good.
- **Timers.** If your payload does something "every hour", keep the time of the
  next run (`time() + 3600`) and compare against `time()`, rather than counting
  loop turns. After waking, the comparison simply says it's due.
- **A new address.** If you showed the address in a notification, show the new
  one. Users will look for the page at the old address otherwise.
- **The ELF loader may be gone.** An app that starts its payload through the
  loader (chapter 8) can find nothing on port 9021 after rest mode. Say so on
  screen instead of failing silently.

## Test it

1. Load `service.elf` and open its page.
2. Put the console in rest mode and wait a minute.
3. Wake it, and open the page again. It should answer within a few seconds of
   the console being back on the network.
4. Read the log:

```
[14:20:03] net: address '192.168.0.10' -> ''
[14:20:03] web: closing the page until the network is back
[14:21:10] net: address '' -> '192.168.0.10'
[14:21:12] web: listening on port 8080
```

Do this test for every payload that serves anything. Most people only notice
rest mode problems the morning after.

Next: [Your first app](05-first-app.md)
