/*
 * PS5 Homebrew Starter - the Notes page: a list that scrolls, written with the
 * on-screen keyboard, saved in /data (docs/08-lists-images-keyboard-sound.md).
 *   Up / Down    choose a note        L2 / R2   a page up / down
 *   Triangle     write a new note     Cross     edit the chosen one
 *   Square       delete it (press twice)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "buttons.hpp"
#include "hello.hpp"
#include "keyboard.hpp"
#include "pad.hpp"
#include "pages.hpp"
#include "paths.hpp"
#include "sound.hpp"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

namespace starter::hello
{
using font::Face;
using screen::Color;

namespace
{
constexpr int max_notes = 50;
constexpr int note_bytes = keyboard::max_length * 4 + 1; // UTF-8: up to 4 bytes a character
constexpr const char *notes_path = STARTER_APP_DATA "/notes.txt";
constexpr const char *notes_temp = STARTER_APP_DATA "/notes.txt.tmp";
constexpr const char *notes_backup = STARTER_APP_DATA "/notes.txt.bak";

char g_notes[max_notes][note_bytes];
int g_count = 0;
bool g_left_out = false; // the file had more than max_notes lines

// ---- Layout ----------------------------------------------------------------
constexpr int card_x = margin, card_w = 1920 - 2 * margin;
constexpr int list_x = card_x + 24, list_w = card_w - 48 - 24; // room for the scrollbar
constexpr int list_y = content_y + 96, list_h = content_h - 96 - 64;
constexpr int row_h = 76;
constexpr int rows_in_view = list_h / row_h;

// Writes a new file next to the old one, then swaps it in: a write that fails
// halfway (full storage, the app closed mid-write) never costs the notes.
bool save() noexcept
{
    mkdir(STARTER_APP_DATA, 0777);
    FILE *f = fopen(notes_temp, "w");
    if (!f)
        return false;
    bool ok = true;
    for (int i = 0; i < g_count; ++i)
        ok = ok && fprintf(f, "%s\n", g_notes[i]) >= 0;
    ok = fclose(f) == 0 && ok;
    if (!ok)
    {
        remove(notes_temp);
        return false;
    }
    // Lines past the 50th weren't loaded: keep the full file as notes.txt.bak
    // before the first save replaces it.
    if (g_left_out)
    {
        rename(notes_path, notes_backup);
        g_left_out = false;
    }
    return rename(notes_temp, notes_path) == 0;
}

// After a change: saved, or the user hears and reads that it wasn't.
void saved_or_say(bool ok) noexcept
{
    sound::play(ok ? sound::Effect::select : sound::Effect::error);
    if (!ok)
        say("Couldn't save the notes");
}

// Copies text into a note, as one line. Text that doesn't fit is cut between
// characters, never in the middle of one (UTF-8 uses up to 4 bytes for one).
void set_note(int index, const char *text) noexcept
{
    char *note = g_notes[index];
    snprintf(note, note_bytes, "%s", text);
    std::size_t n = strlen(note);
    if (text[n] != '\0') // cut: back up to the start of a character
    {
        while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xc0) == 0x80)
            --n;
        note[n] = '\0';
    }
    for (char *p = note; *p; ++p)
        if (*p == '\n' || *p == '\r')
            *p = ' ';
}

void delete_note(int index) noexcept
{
    for (int i = index; i + 1 < g_count; ++i)
        memcpy(g_notes[i], g_notes[i + 1], sizeof g_notes[i]);
    --g_count;
}

// A note as it fits in `room` pixels: whole, or cut between characters with
// "…" at the end. buf holds the cut version.
const char *fit(const char *note, int room, char *buf) noexcept
{
    constexpr const char *dots = "\xE2\x80\xA6"; // …
    if (screen::Canvas::text_width(note, Face::medium, 22) <= static_cast<float>(room))
        return note;
    // The longest start that fits, found by halving: lo always fits, hi doesn't.
    const int len = static_cast<int>(strlen(note));
    int lo = 0, hi = len;
    while (hi - lo > 1)
    {
        int mid = (lo + hi) / 2;
        while (mid > lo && (static_cast<unsigned char>(note[mid]) & 0xc0) == 0x80)
            --mid; // to the start of a character
        if (mid == lo)
            break;
        memcpy(buf, note, static_cast<std::size_t>(mid));
        strcpy(buf + mid, dots);
        if (screen::Canvas::text_width(buf, Face::medium, 22) <= static_cast<float>(room))
            lo = mid;
        else
            hi = mid;
    }
    memcpy(buf, note, static_cast<std::size_t>(lo));
    strcpy(buf + lo, dots);
    return buf;
}

// Keeps the chosen row in view: moves the first row only as far as needed.
void keep_in_view(State &s) noexcept
{
    if (s.notes_selected < s.notes_first)
        s.notes_first = s.notes_selected;
    if (s.notes_selected >= s.notes_first + rows_in_view)
        s.notes_first = s.notes_selected - rows_in_view + 1;
    const int last_first = g_count > rows_in_view ? g_count - rows_in_view : 0;
    if (s.notes_first > last_first)
        s.notes_first = last_first;
    if (s.notes_first < 0)
        s.notes_first = 0;
}

// What the keyboard brought back: a new note, or an edited one.
bool finish_typing(State &s) noexcept
{
    const keyboard::State k = keyboard::poll();
    if (k == keyboard::State::open || k == keyboard::State::idle)
        return false;
    const int editing = s.editing;
    s.editing = -2;
    if (k != keyboard::State::accepted)
    {
        sound::play(sound::Effect::back);
        if (k == keyboard::State::failed)
            say("The keyboard couldn't open");
        return true;
    }
    const char *text = keyboard::text();
    if (!text[0])
    {
        sound::play(sound::Effect::back);
        say("Nothing typed: nothing saved");
        return true;
    }
    if (editing >= 0 && editing < g_count)
        set_note(editing, text);
    else if (notes_add(text))
        s.notes_selected = g_count - 1;
    else
    {
        sound::play(sound::Effect::error);
        say("The list is full: delete a note first");
        return true;
    }
    keep_in_view(s);
    saved_or_say(save());
    return true;
}
} // namespace

void notes_load() noexcept
{
    g_count = 0;
    g_left_out = false;
    FILE *f = fopen(notes_path, "r");
    if (!f)
        return;
    char line[note_bytes + 2];
    while (fgets(line, sizeof line, f))
    {
        // A line longer than a note (the file edited by hand): one note all
        // the same, the rest of the line skipped.
        if (!strchr(line, '\n') && !feof(f))
            for (int ch = fgetc(f); ch != EOF && ch != '\n'; ch = fgetc(f))
            {
            }
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0])
            continue;
        if (g_count == max_notes)
        {
            g_left_out = true; // more lines than notes: see save()
            break;
        }
        set_note(g_count++, line);
    }
    fclose(f);
}

bool notes_add(const char *text) noexcept
{
    if (g_count >= max_notes)
        return false;
    set_note(g_count++, text);
    return true;
}

bool notes_update(unsigned pressed) noexcept
{
    State &s = state();
    bool redraw = false;

    // The keyboard is open: wait for it. (The controller is the keyboard's.)
    if (s.editing != -2)
        return finish_typing(s);

    // A first Square press runs out after 3 seconds.
    if (s.delete_armed >= 0 && screen::now_ms() > s.delete_until_ms)
    {
        s.delete_armed = -1;
        redraw = true;
    }

    if (pressed)
    {
        const int before = s.notes_selected;
        if ((pressed & pad::up) && s.notes_selected > 0)
            --s.notes_selected;
        if ((pressed & pad::down) && s.notes_selected + 1 < g_count)
            ++s.notes_selected;
        if (pressed & pad::l2) // a page up
            s.notes_selected = s.notes_selected > rows_in_view ? s.notes_selected - rows_in_view : 0;
        if ((pressed & pad::r2) && g_count > 0) // a page down
            s.notes_selected = s.notes_selected + rows_in_view < g_count ? s.notes_selected + rows_in_view
                                                                         : g_count - 1;
        if (s.notes_selected != before)
        {
            s.delete_armed = -1;
            sound::play(sound::Effect::move);
        }

        // One of new, edit and delete per frame: two buttons pressed together
        // must not, say, delete the note the keyboard was just opened for.
        if (pressed & pad::triangle)
        {
            if (g_count >= max_notes)
            {
                sound::play(sound::Effect::error);
                say("The list is full: delete a note first");
            }
            else if (keyboard::open("New note", "Write something", "", keyboard::max_length))
            {
                s.editing = -1;
                sound::play(sound::Effect::select);
            }
            else
                say("The keyboard couldn't open");
        }
        else if ((pressed & pad::cross) && g_count > 0)
        {
            if (keyboard::open("Edit note", "", g_notes[s.notes_selected], keyboard::max_length))
            {
                s.editing = s.notes_selected;
                sound::play(sound::Effect::select);
            }
            else
                say("The keyboard couldn't open");
        }
        else if ((pressed & pad::square) && g_count > 0)
        {
            if (s.delete_armed == s.notes_selected)
            {
                delete_note(s.notes_selected);
                s.delete_armed = -1;
                if (s.notes_selected >= g_count && s.notes_selected > 0)
                    --s.notes_selected;
                if (save())
                {
                    sound::play(sound::Effect::back);
                    say("Note deleted");
                }
                else
                    saved_or_say(false);
            }
            else
            {
                // Deleting is for good: it takes a second press.
                s.delete_armed = s.notes_selected;
                s.delete_until_ms = screen::now_ms() + 3000;
                sound::play(sound::Effect::error);
                say("Press Square again to delete");
            }
        }
        keep_in_view(s);
        redraw = true;
    }

    // Smooth scrolling: the shown position eases a quarter of the way to its
    // target every frame, so the list glides instead of jumping.
    const float target = static_cast<float>(s.notes_first * row_h);
    const float gap = target - s.notes_scroll;
    if (gap != 0)
    {
        s.notes_scroll = (gap > -0.5f && gap < 0.5f) ? target : s.notes_scroll + gap * 0.25f;
        redraw = true;
    }
    return redraw;
}

void notes_draw(screen::Canvas &c) noexcept
{
    const State &s = state();
    c.round_rect(card_x, content_y, card_w, content_h, 18, Color::surface);
    c.round_frame(card_x, content_y, card_w, content_h, 18, 2, Color::line);
    c.text(card_x + 32, content_y + 62, "Notes", Face::semibold, 28, Color::text);
    char count[32];
    snprintf(count, sizeof count, "%d / %d", g_count, max_notes);
    c.text(card_x + 140, content_y + 62, count, Face::mono_medium, 18, Color::faint);

    if (g_count == 0)
    {
        const char *empty = "No notes yet. Press Triangle to write one.";
        const float w = screen::Canvas::text_width(empty, Face::regular, 24);
        c.text((1920 - w) / 2, list_y + list_h / 2.0f, empty, Face::regular, 24, Color::muted);
    }
    else
    {
        // Rows are drawn where the scroll puts them; the clip cuts off the
        // ones partly outside the list.
        c.clip(list_x, list_y, list_w, list_h);
        const int first = static_cast<int>(s.notes_scroll) / row_h;
        for (int i = first; i < g_count && i <= first + rows_in_view + 1; ++i)
        {
            const float y = static_cast<float>(list_y + i * row_h) - s.notes_scroll;
            const int top = static_cast<int>(y);
            const bool chosen = i == s.notes_selected;
            const bool armed = i == s.delete_armed;
            if (chosen)
                c.round_rect(list_x, top + 4, list_w, row_h - 8, 12, armed ? Color::danger : Color::hover,
                             armed ? 60 : 255);
            char number[8];
            snprintf(number, sizeof number, "%2d", i + 1);
            c.text(static_cast<float>(list_x + 20), y + 47, number, Face::mono_medium, 18, Color::faint);
            char shown[note_bytes + 4];
            c.text(static_cast<float>(list_x + 76), y + 47, fit(g_notes[i], list_w - 96, shown), Face::medium,
                   22, chosen ? Color::text : Color::muted);
        }
        c.unclip();

        // The scrollbar: the thumb's size and place show what part is in view.
        if (g_count > rows_in_view)
        {
            const int track_x = card_x + card_w - 30, track_h = list_h;
            c.round_rect(track_x, list_y, 6, track_h, 3, Color::line);
            const float shown = static_cast<float>(rows_in_view) / static_cast<float>(g_count);
            const int thumb_h = static_cast<int>(static_cast<float>(track_h) * shown);
            const float most = static_cast<float>((g_count - rows_in_view) * row_h);
            // 0 at the top, 1 at the bottom. (Right after a delete the scroll
            // is still easing back, briefly past the new bottom.)
            float at = s.notes_scroll / most;
            at = at < 0 ? 0 : at > 1 ? 1 : at;
            const int thumb_y = list_y + static_cast<int>((track_h - thumb_h) * at);
            c.round_rect(track_x, thumb_y, 6, thumb_h, 3, Color::muted);
        }
    }

    using namespace buttons;
    static const Hint what_does_what[] = {
        {dpad_up, dpad_down, "choose"}, {l2, r2, "page"},      {triangle, none, "new"},
        {cross, none, "edit"},          {square, none, "delete"},
    };
    hints(c, card_x + 32, content_y + content_h - 38, what_does_what);
}
} // namespace starter::hello
