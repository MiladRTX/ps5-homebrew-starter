/*
 * PS5 Homebrew Starter - pictures (image.hpp), decoded with stb_image.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "image.hpp"

#include "stb_image.h"

#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace starter::image
{
bool load(const char *path, Image &out) noexcept
{
    unload(out);
    // The whole file into memory (like the fonts), then decoded from there.
    const int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    struct stat st = {};
    unsigned char *data = nullptr;
    std::size_t got = 0;
    if (fstat(fd, &st) == 0 && st.st_size > 0 && st.st_size < (32 << 20))
    {
        data = static_cast<unsigned char *>(malloc(static_cast<std::size_t>(st.st_size)));
        while (data && got < static_cast<std::size_t>(st.st_size))
        {
            const ssize_t n = read(fd, data + got, static_cast<std::size_t>(st.st_size) - got);
            if (n <= 0)
                break;
            got += static_cast<std::size_t>(n);
        }
    }
    close(fd);
    if (!data || got != static_cast<std::size_t>(st.st_size))
    {
        free(data);
        return false;
    }

    int w = 0, h = 0, channels = 0;
    // RGBA bytes, which as 32-bit numbers are 0xAABBGGRR: the screen's own format.
    unsigned char *rgba = stbi_load_from_memory(data, static_cast<int>(got), &w, &h, &channels, 4);
    free(data);
    if (!rgba)
        return false;
    out.w = w;
    out.h = h;
    out.pixels = reinterpret_cast<std::uint32_t *>(rgba);
    return true;
}

void unload(Image &image) noexcept
{
    stbi_image_free(image.pixels);
    image = Image{};
}
} // namespace starter::image
