/*
 * PS5 Homebrew Starter - where the app's files are. On the PS5, the app's own
 * folder is /app0 and the console's storage is /data. The PC simulator
 * (app/sim) runs in build/sim, with its own app0 and data folders there.
 * Write paths as STARTER_DATA "/my-app/settings.txt" and they work in both.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#ifdef STARTER_SIM
#define STARTER_APP0 "app0"
#define STARTER_DATA "data"
#else
#define STARTER_APP0 "/app0"
#define STARTER_DATA "/data"
#endif
