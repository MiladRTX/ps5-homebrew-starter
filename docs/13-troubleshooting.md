# 13. Troubleshooting

Problems that actually came up while building Breeze and this starter, by
what you see.

## Building

**`$'\r': command not found`, or `bad interpreter`**
The scripts got Windows line endings, usually from a Git checkout on Windows.
Fix the files with `sed -i 's/\r$//' tools/setup.sh app/build.sh app/preview/run.sh`,
and stop it from happening again with `git config --global core.autocrlf false`
before cloning. The repository's `.gitattributes` asks Git for Linux line
endings too.

**`No payload SDK at ...: run tools/setup.sh first`**
Exactly that. If you installed the SDK somewhere else, point to it:
`make PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk`.

**Odd failures in a folder with spaces in its path**
The kit's scripts can't handle spaces, which is why `app/build.sh` builds the
kit in `~/.ps5-starter/kit-work`. Your own scripts should quote every path (`"$dir"`).

**The first app build fails while downloading**
The kit downloads its SDK and zlib on the first build. Run it again. Nothing
is half-installed; it starts the failed download over.

**`undefined symbol: sce...` when the app links**
The payload SDK has no stub for that system function. Add a link-only stub
for its library, the way the keyboard's `sceCommonDialogInitialize` is done
(chapter 8, *A function the SDK doesn't have*). Looking the function up while
the app runs instead doesn't work: the console refuses.

**`undefined symbol: __emutls_get_address`**
Something uses `thread_local` variables, which the app's runtime doesn't have.
stb_image did, until `STBI_NO_THREAD_LOCALS` in `stb_impl.c`. Turn them off in
the library, or use plain variables.

## Payloads

**Loaded, but nothing happens**
- Look for a notification: the service shows one when it starts, and one
  when it can't.
- Read its log over FTP (`/data/ps5-starter/service.log`).
- Look for it in Payload Manager's process list. If it's not there, it
  crashed or exited.
- A crash right at start is often a system function your firmware doesn't
  have. Declare those with `__attribute__((weak))` and check them for `NULL`
  (chapter 2).

**`make send`: connection refused**
Your ELF loader only accepts connections from the console itself. Load it with
Payload Manager instead.

**Two copies running**
Each load starts a new process. Use the single-copy lock from chapter 3.

**It dies when a browser closes the page early**
That's `SIGPIPE`. Ignore it and set `SO_NOSIGPIPE` (chapter 3).

**Its page doesn't open after rest mode**
See [chapter 4](04-surviving-rest-mode.md). The same can happen to your FTP
server and ELF loader: restart them after waking if they stop answering.

**A system function you found somewhere hangs or crashes the payload**
Functions without documentation are declared from guesses, and a wrong guess
can crash the process, or hang it so badly that only a console restart ends
it. That happened during Breeze's development. Try unknown functions in a
small separate test payload first, never in the payload people are using.

**HTTPS requests fail with `0x8095f00c`**
The system's own HTTPS can't find its certificate list from a payload, so
every site fails the certificate check. Don't turn the check off. The kit's
[`docs/CURL.md`](../kit/docs/CURL.md) shows a working alternative: curl with OpenSSL and the
console's own certificate file. Or avoid needing HTTPS in a payload at all.

## Apps

**The app doesn't appear on the home screen**
- Wait 15 seconds: ShadowMountPlus checks `/data/homebrew` periodically.
- The folder must be `/data/homebrew/<TITLE ID>/` with `eboot.bin` directly
  inside it. Unzipping into a folder of its own often gives you
  `/data/homebrew/PPSA99901/PPSA99901/` by mistake.
- The folder name must match `titleId` in `sce_sys/param.json`.
- ShadowMountPlus has to be running.

**"the app's fonts are missing"**
`assets/fonts/` isn't in the app folder. Copy the whole folder the build
made, not just `eboot.bin`.

**Your changes don't show up**
- Did you close the app before copying? Copying over an open app can leave
  a mix of old and new files. Close it and copy again.
- Check you copied the new build: look at the file times in `build/app/`.

**The background service says "Not running"**
- The message at the bottom tells you which: no ELF loader on port 9021, or
  `service.elf` missing from the app.
- After rest mode, the loader may have stopped answering. Restart it, then
  reopen the app.
- If the loader is fine, the service started and then crashed: read its log.

## The PC simulator

**`No display to open a window on`, or no window appears**
WSL shows Linux windows on your Windows desktop with its built-in display
(WSLg). Update WSL with `wsl --update` in PowerShell, then `wsl --shutdown`, and
open Ubuntu again. On Linux, run it from a desktop session.

**The simulator says the service isn't running**
- Another program on your PC may use port 8080. The service's log says so:
  `build/sim/data/ps5-starter/service.log`. Change the port in `service.c`
  (`PORT`) and in `hello.hpp` (`service_port`).
- A service left over from an earlier run may be stuck: `app/sim/run.sh stop`,
  then run the simulator again.

**My controller doesn't do anything in the simulator**
Read `build/sim/bridge.log` (chapter 10 explains the bridge):
- *no controller yet*: turn it on, or connect it. The bridge keeps looking,
  every second.
- No log, or `no Python on Windows`: install Python from python.org, then run
  the simulator again.
- `no simulator on port 9278`, or the simulator says `port 9278 is busy`:
  something else uses that port. Pick another for both:
  `STARTER_SIM_BRIDGE_PORT=9300 app/sim/run.sh`.

The keyboard always works.

**No sound in the simulator**
The terminal says `simulator: no sound (...)` with the reason. In WSL, sound
comes through the same built-in display support as the window: `wsl --update`
fixes most cases. Everything else works without sound.

**It's smooth in the simulator but slow on the PS5**
Your PC draws much faster than the console. Draw less per frame, or only when
something changed (chapter 6), and check on the console.

## Copying files

**Git Bash turns `/data/homebrew` into `C:/Program Files/Git/data/homebrew`**
Git Bash on Windows rewrites anything that looks like a Unix path. Run those
commands in WSL instead, or put `MSYS_NO_PATHCONV=1` in front of them.

**The FTP server stops answering after rest mode**
Restart it. Many FTP servers don't reopen their port after waking.

## Still stuck?

Shrink the problem: the smallest payload or app that still shows it. Half the
time that's where the answer turns up. The rest of the time, it's exactly what
to post when you ask for help, together with your log.
