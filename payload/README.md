# Payloads

Two starters, each built with `make` in its own folder:

- [`hello/`](hello): shows a notification and exits. [Chapter 2](../docs/02-first-payload.md).
- [`service/`](service): a background service with a status page on port 8080, one copy at a time, a log, and rest mode handled. [Chapters 3](../docs/03-background-payload.md) and [4](../docs/04-surviving-rest-mode.md).

Run `tools/setup.sh` once first; it installs the payload SDK the Makefiles use.

`service/sim_stubs.c` stands in for the console's functions so the same
`service.c` also runs on a PC, for the app's simulator (`app/sim/run.sh`).
It's never part of the PS5 build.
