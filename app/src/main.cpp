/*
 * PS5 Homebrew Starter - where the app starts, on the PS5 and in the PC
 * simulator (app/sim) alike. Only the PC preview (app/preview) has its own main().
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hello.hpp"
#include "pad.hpp"
#include "paths.hpp"
#include "screen.hpp"

int main()
{
    // The fonts travel inside the app; /app0 is the app's own folder.
    if (!starter::font::load(STARTER_APP0 "/assets/fonts/"))
        starter::screen::notify("Hello PS5: the app's fonts are missing");
    (void)starter::pad::init();
    starter::hello::load();
    // Calls update about 60 times a second and draws when it returns true.
    // Never returns: the PS5 closes the app when you leave it.
    starter::screen::run(starter::hello::update, starter::hello::draw);
}
