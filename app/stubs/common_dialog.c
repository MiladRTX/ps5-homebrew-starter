/*
 * PS5 Homebrew Starter - a link-only stub for sceCommonDialogInitialize, which
 * the on-screen keyboard needs (app/src/keyboard.cpp) but the PS5 payload SDK
 * has no stub for. app/build.sh builds it as libSceCommonDialog.so; the kit
 * reads it to learn that the function lives in the system's
 * libSceCommonDialog.sprx, and the console links the app to the real one when
 * it starts. This body never runs and isn't part of the app.
 * The same approach as ProsperoStore's, Copyright (C) 2026 BlackBearReloaded.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

int sceCommonDialogInitialize(void)
{
    return 0;
}
