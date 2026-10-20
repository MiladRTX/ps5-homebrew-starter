/* The service's version, in one place: service.c reports it on /status, and
 * the app is built with the same file, so it knows which version it carries
 * and can replace an older one that's still running (docs/09-app-and-payload.md).
 * Raise it whenever you change the service. */
#pragma once

#define SERVICE_VERSION "1.0"
