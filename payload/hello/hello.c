/* Your first PS5 payload: shows a notification, writes a line to a file,
 * and exits. Build it with `make`, then load hello.elf on the console
 * (docs/02-first-payload.md). */
#include <stdio.h>
#include <string.h>

/* The PS5's notification pop-up. The system library provides the function;
 * declaring it here is all it takes to call it. */
typedef struct notify_request {
    char reserved[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int device, notify_request_t *request, size_t size,
                                     int blocking);

static void notify(const char *message) {
    notify_request_t request;
    memset(&request, 0, sizeof request);
    snprintf(request.message, sizeof request.message, "%s", message);
    sceKernelSendNotificationRequest(0, &request, sizeof request, 0);
}

int main(void) {
    notify("Hello from my first payload!");

    /* /data is the console's own storage, readable over FTP. */
    FILE *f = fopen("/data/hello.txt", "w");
    if (f) {
        fprintf(f, "hello.elf ran\n");
        fclose(f);
    }
    return 0;
}
