/* Stand-ins for the console functions service.c uses, so the same service.c
 * also builds as a normal PC program for the app's PC simulator (app/sim).
 * Never part of the PS5 build: the Makefile only compiles service.c. */
#include <stdio.h>
#include <string.h>

typedef struct notify_request {
    char reserved[45];
    char message[3075];
} notify_request_t;

/* A notification: printed, one line. The simulator starts the service with its
 * output going nowhere (link.cpp), so look for these in service.log instead:
 * service.c's notify() writes each one there as a "notify:" line. */
int sceKernelSendNotificationRequest(int device, notify_request_t *request, size_t size,
                                     int blocking) {
    (void)device;
    (void)size;
    (void)blocking;
    char line[sizeof request->message];
    snprintf(line, sizeof line, "%s", request->message);
    for (char *p = line; *p; p++)
        if (*p == '\n')
            *p = ' ';
    printf("service notify: %s\n", line);
    fflush(stdout);
    return 0;
}

/* The CPU temperature: the PC's own sensor if Linux has one (a real Linux PC
 * usually does; WSL usually doesn't, and then the app shows "-"). */
int sceKernelGetCpuTemperature(int *celsius) {
    *celsius = -1;
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!f)
        return -1;
    int milli = 0;
    if (fscanf(f, "%d", &milli) == 1 && milli > 0)
        *celsius = milli / 1000;
    fclose(f);
    return *celsius > 0 ? 0 : -1;
}
