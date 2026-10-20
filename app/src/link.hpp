/*
 * PS5 Homebrew Starter - talking to a background payload on the same console.
 * The app and the payload are separate processes; they talk over HTTP on
 * 127.0.0.1, like a web page would (docs/09-app-and-payload.md).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstddef>

namespace starter::link
{
// GET path from 127.0.0.1:port. The answer's body goes to out (cut to cap).
// Returns the HTTP status, 0 when nothing is listening, -1 when the answer
// isn't HTTP or the path is too long (over about 400 characters).
int http_get(int port, const char *path, char *out, std::size_t cap) noexcept;

// Sends the ELF at path (a file in the app, e.g. /app0/assets/service.elf) to
// the ELF loader on 127.0.0.1:9021, which starts it. 0 when it was sent, -1
// when the file can't be read, -2 when no loader answered, -3 when it isn't an
// ELF or is bigger than 4 MB. (The simulator: -4 when the PC program won't start.)
int start_payload(const char *path) noexcept;

// The console's address on the network, like "192.168.0.10": where a phone or
// PC opens the service's page. In the PC simulator "localhost", because its
// service runs on the PC itself. False when there's no network.
bool console_address(char *out, std::size_t cap) noexcept;

// The number in a JSON answer for "key":<number>; fallback when it's missing.
[[nodiscard]] long json_number(const char *json, const char *key, long fallback) noexcept;

// The text in a JSON answer for "key":"<text>" into out; "" when it's missing.
void json_text(const char *json, const char *key, char *out, std::size_t cap) noexcept;

// -1, 0 or 1 as version a is older than, the same as, or newer than b, number
// by number: "1.9" < "1.10" < "2.0", and "1.0" is the same as "1.0.0".
[[nodiscard]] int compare_versions(const char *a, const char *b) noexcept;
} // namespace starter::link
