# 2. Your first payload

A **payload** is a program in ELF format, the same kind of file a Linux or
FreeBSD program is. An **ELF loader** on the console receives it and runs it as
a process. A payload has no icon and no window: it runs, does its job, and
either exits or keeps running in the background.

That makes payloads the easiest place to start: one C file, one command.

## The code

[`payload/hello/hello.c`](../payload/hello/hello.c), all of it:

```c
typedef struct notify_request {
    char reserved[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int device, notify_request_t *request, size_t size,
                                     int blocking);
```

Functions starting with `sce` belong to the PS5's own libraries. There's no
header for most of them, so you declare the function yourself, exactly like
this, and call it. The SDK links your program against the console's library,
and the loader connects the call to the real function when the payload starts.

```c
int main(void) {
    notify("Hello from my first payload!");

    FILE *f = fopen("/data/hello.txt", "w");
    ...
    return 0;
}
```

Everything else is ordinary C: `fopen`, `printf`, sockets and threads all work.
`/data` is the console's own storage, the part you can read and write over FTP.

## Build it

```bash
make -C payload/hello
```

```
prospero-clang -Wall -Wextra -O2 -o hello.elf hello.c
```

That's `payload/hello/hello.elf`, ready to run.

## Run it

Pick whichever your console has.

**With Payload Manager.** Open its dashboard in a browser on your phone or PC
(usually `http://<PS5 IP>:8084`), choose **Upload ELF Payload**, pick
`hello.elf`, then launch it.

**With an ELF loader that accepts network connections.** Send it straight from WSL:

```bash
make -C payload/hello send PS5_HOST=192.168.0.10     # your PS5's address
```

If that says *connection refused*, your loader only listens on the console
itself. Use Payload Manager instead.

## See it work

A notification pops up in the corner of the screen: **Hello from my first
payload!**

The file it wrote is on the console now. Read it over FTP (change the address
and the port to yours):

```bash
curl ftp://192.168.0.10:2121/data/hello.txt
```

```
hello.elf ran
```

That's the whole loop of payload development: **edit, `make`, load, look**.

## What happened

1. The loader received `hello.elf`, started it as a new process and called `main`.
2. `main` asked the system for a notification and wrote a file.
3. `main` returned, so the process ended. Load it again and it runs again.

## Try this

- Change the message, build, load it again.
- Write the time into the file: `time()` and `strftime()` work as usual.
- Show the CPU temperature. Declare the function like the notification one:

  ```c
  int sceKernelGetCpuTemperature(int *celsius) __attribute__((weak));
  ```

  `weak` means: if a firmware doesn't have this function, its address is `NULL`
  instead of the payload refusing to load. Check `if (sceKernelGetCpuTemperature)`
  before calling it. Chapter 3's service does exactly this.

## Good to know

- **A payload can read and write all of `/data`**, including other apps'
  settings. Be careful with anything that deletes or overwrites files.
- **Every load starts a new process.** Loading a payload that never exits twice
  gives you two copies running. Chapter 3 shows how to keep it to one.
- **`printf` output isn't on your screen.** Depending on the loader it's logged
  somewhere or dropped. Write to your own log file instead (chapter 3).

Next: [A real background payload](03-background-payload.md)
