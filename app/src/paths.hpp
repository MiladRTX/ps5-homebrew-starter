/*
 * PS5 Homebrew Starter - where the app's files are. On the PS5, the app's own
 * folder is /app0 and the console's storage is /data. The PC builds run in a
 * work folder with their own app0 and data folders: the simulator (app/sim)
 * in build/sim, the preview (app/preview) in an empty temporary folder.
 * Write paths as STARTER_APP_DATA "/settings.txt" (this app's folder in /data)
 * and they work everywhere.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#ifdef STARTER_PREVIEW // the PC builds: the simulator defines it too
#define STARTER_APP0 "app0"
#define STARTER_DATA "data"
#else
#define STARTER_APP0 "/app0"
#define STARTER_DATA "/data"
#endif

// This app's own folder in /data: its settings and notes. One place, so
// tools/new-app.py renames it for every file at once.
#define STARTER_APP_DATA STARTER_DATA "/ps5-starter"
