/* The service's name and version, in one place: service.c reports both on
 * /status, and the app is built with the same file, so it knows which service
 * is its own and which version it carries, and can replace an older one
 * that's still running (docs/09-app-and-payload.md).
 * Raise the version whenever you change the service. */
#pragma once

#define SERVICE_NAME "service"
#define SERVICE_VERSION "1.0"
