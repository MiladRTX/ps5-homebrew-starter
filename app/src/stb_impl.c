/*
 * PS5 Homebrew Starter - stb_truetype's and stb_image's implementations
 * (public domain / MIT, by Sean Barrett and contributors), compiled once here.
 */
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"

/* The app's runtime has no __assert; the checks only guard against broken
 * files, and the app ships its own. */
#define STBTT_assert(x) ((void)0)
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

/* Images: PNG and JPEG, read from memory (image.cpp reads the file). */
#define STBI_ASSERT(x) ((void)0)
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS /* the app runtime has no thread-local storage */
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
