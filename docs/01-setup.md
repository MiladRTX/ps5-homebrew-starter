# 1. Set up your PC

PS5 homebrew is built on a PC and copied to the console. The tools run on
Linux; on Windows you use WSL, which is Linux running inside Windows. This
chapter takes about 15 minutes, most of it downloads.

## Windows: install WSL

Open **PowerShell as administrator** and run:

```powershell
wsl --install -d Ubuntu
```

Restart when it asks, then open **Ubuntu** from the Start menu and pick a user
name and password. From now on, every command in this guide runs in that
Ubuntu window unless it says otherwise.

Your Windows drives are inside WSL under `/mnt/`: `C:\Users\you\Documents` is
`/mnt/c/Users/you/Documents`.

> **Tip:** keep projects in a folder without spaces in its path. Some build
> scripts can't handle spaces. This starter works around it, but your own
> scripts might not.

## Get this starter

In the Ubuntu window, clone it into your home folder and go into it:

```bash
git clone https://github.com/MiladRTX/ps5-homebrew-starter.git
cd ps5-homebrew-starter
```

Cloning inside WSL, rather than into a Windows folder, makes builds much
faster: WSL reads its own files quicker than Windows' drives.

## Install the tools

```bash
tools/setup.sh
```

This does two things:

1. It installs the compiler (clang 18), the linker, make, git, Python, SDL2
   (for running apps in a window on your PC, chapter 10) and a few helpers with
   `apt`. It asks for your password once.
2. It downloads the **PS5 payload SDK** v0.42 and checks it against its known
   SHA-256 before using it, so a broken or tampered download is refused. It
   goes into `~/.ps5-starter/ps5-payload-sdk`; nothing else on your system changes.

Run it again any time: whatever's already set up is skipped. To only check
without installing anything:

```bash
tools/setup.sh check
```

```
packages: ok
payload SDK: ok (/home/you/.ps5-starter/ps5-payload-sdk)
```

## What the SDK is

The payload SDK is a complete toolchain for the PS5's operating system, which is
based on FreeBSD:

- `bin/prospero-clang` is clang set up for the PS5: same compiler, PS5 target
- the C library, POSIX headers (sockets, threads, files) and stubs for the
  console's own libraries
- `samples/`: small programs for one feature each. Worth a read once you've
  finished chapter 2.

The app kit used in chapter 5 downloads its own copy of the same SDK, so you
don't set anything up for it.

## On the console

Find your PS5's IP address: **Settings → Network → Connection Status → View
Connection Status**, the IPv4 address. You'll use it to copy files over FTP and,
later, to open the service's status page.

To copy files you need an FTP client: [WinSCP](https://winscp.net) or
[FileZilla](https://filezilla-project.org) on Windows, or `curl` in WSL.
Connect to your PS5's address on the port your FTP server uses (often 2121).

Next: [Your first payload](02-first-payload.md)
