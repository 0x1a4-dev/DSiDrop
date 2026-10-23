#include "app.h"
#include <nds.h>
#include <dswifi9.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool transient(void) {
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS || errno == EALREADY;
}

static uint64_t timeoutTicks(unsigned frames) {
    return (uint64_t)frames * TICK_FREQ / 60;
}

void httpClose(Http *http) {
    if (http->socket >= 0) {

        forceclosesocket(http->socket);
        http->socket = -1;
    }
}

static int receive(Http *http, void *buffer, size_t length) {
    uint64_t started = tickGetCount();
    uint64_t timeout = timeoutTicks(http->timeoutFrames);
    for (;;) {
        if (!appPoll()) return -1;
        int n = recv(http->socket, buffer, length, 0);
        if (n >= 0) return n;
        if (!transient()) { snprintf(lastError, 192, "Network error (%d)", errno); return -1; }
        if (tickGetCount() - started >= timeout) { strcpy(lastError, "PC response timed out"); return -1; }
        threadSleep(1000);
    }
}

bool httpOpen(Http *http, const char *method, const char *path, uint32_t offset) {
    return httpOpenTimeout(http, method, path, offset, 60 * 30);
}

bool httpOpenTimeout(Http *http, const char *method, const char *path, uint32_t offset, unsigned timeoutFrames) {
    memset(http, 0, sizeof(*http));
    http->timeoutFrames = timeoutFrames;
    unsigned connectTimeout = timeoutFrames < 60 * 15 ? timeoutFrames : 60 * 15;
    http->socket = -1;
    struct sockaddr_in server = {0};
    server.sin_family = AF_INET;
    server.sin_port = htons(settings.port);
    if (!inet_aton(settings.host, &server.sin_addr)) {
        strcpy(lastError, "Server address must be IPv4"); return false;
    }
    http->socket = socket(AF_INET, SOCK_STREAM, 0);
    if (http->socket < 0) { strcpy(lastError, "No network socket available"); return false; }
    unsigned long nonblock = 1;
    if (ioctl(http->socket, FIONBIO, &nonblock) < 0) goto failed;
    int result = connect(http->socket, (struct sockaddr *)&server, sizeof(server));
    if (result < 0 && !transient()) {
        strcpy(lastError, "Could not connect to the PC"); goto failed;
    }
    uint64_t started = tickGetCount();
    uint64_t timeout = timeoutTicks(connectTimeout);
    for (;;) {
        struct sockaddr_in peer;
        socklen_t length = sizeof(peer);
        if (getpeername(http->socket, (struct sockaddr *)&peer, &length) == 0) break;
        if (tickGetCount() - started >= timeout) {
            strcpy(lastError, "PC unreachable. Check IP and firewall."); goto failed;
        }
        if (!appYield()) goto failed;
    }
    char request[1024], range[64] = "";
    if (offset) snprintf(range, sizeof(range), "Range: bytes=%lu-\r\n", (unsigned long)offset);
    int requestSize = snprintf(request, sizeof(request),
        "%s %s HTTP/1.1\r\nHost: %s:%u\r\nUser-Agent: " APP_NAME "/" APP_VERSION "\r\n"
        "Accept-Encoding: identity\r\nConnection: close\r\n%sContent-Length: 0\r\n\r\n",
        method, path, settings.host, settings.port, range);
    if (requestSize < 0 || requestSize >= (int)sizeof(request)) goto failed;
    int sent = 0;
    started = tickGetCount();
    while (sent < requestSize) {
        int n = send(http->socket, request + sent, requestSize - sent, 0);
        if (n > 0) { sent += n; started = tickGetCount(); }
        else if (n < 0 && !transient()) goto failed;
        else {
            if (tickGetCount() - started >= timeout) { strcpy(lastError, "PC request timed out"); goto failed; }
            if (!appYield()) goto failed;
        }
    }

    char *headerEnd = NULL;
    while (http->used < sizeof(http->buffer) - 1) {
        int n = receive(http, http->buffer + http->used, sizeof(http->buffer) - 1 - http->used);
        if (n <= 0) goto failed;
        http->used += n;
        http->buffer[http->used] = 0;
        headerEnd = strstr((char *)http->buffer, "\r\n\r\n");
        if (headerEnd) break;
    }
    if (!headerEnd) { strcpy(lastError, "HTTP headers are too large"); goto failed; }
    http->pos = (unsigned)(headerEnd + 4 - (char *)http->buffer);
    *headerEnd = 0;
    if (sscanf((char *)http->buffer, "HTTP/%*u.%*u %d", &http->status) != 1) goto failed;
    bool hasLength = false;
    char *line = strstr((char *)http->buffer, "\r\n");
    while (line) {
        line += 2;
        char *next = strstr(line, "\r\n");
        if (next) *next = 0;
        if (!strncasecmp(line, "Content-Length:", 15)) {
            char *end;
            errno = 0;
            unsigned long n = strtoul(line + 15, &end, 10);
            while (*end == ' ') end++;
            if (errno || *end || n > MAX_GAME_SIZE) goto failed;
            http->length = n; hasLength = true;
        }
        if (!strncasecmp(line, "Transfer-Encoding:", 18) ||
            (!strncasecmp(line, "Content-Encoding:", 17) && strcasecmp(line + 18, "identity"))) {
            strcpy(lastError, "Unsupported HTTP compression"); goto failed;
        }
        if (!strncasecmp(line, "Content-Range:", 14)) {
            unsigned long start, end, total;
            if (sscanf(line + 14, " bytes %lu-%lu/%lu", &start, &end, &total) != 3 ||
                start > end || total > MAX_GAME_SIZE || end >= total) goto failed;
            http->rangeStart = start; http->rangeTotal = total;
        }
        line = next;
    }
    if (!hasLength) { strcpy(lastError, "PC response has no file length"); goto failed; }
    return true;
failed:
    if (!lastError[0]) strcpy(lastError, "HTTP connection failed");
    httpClose(http);
    return false;
}

int httpRead(Http *http, void *buffer, size_t length) {
    if (http->pos < http->used) {
        unsigned n = http->used - http->pos;
        if (n > length) n = length;
        memcpy(buffer, http->buffer + http->pos, n);
        http->pos += n;
        return n;
    }
    return receive(http, buffer, length);
}

bool httpText(const char *method, const char *path, char *output, size_t capacity) {
    // Metadata requests are sequential on the main thread. Keep the 8 KiB
    // receive buffer out of its small DTCM stack, including during catalog load.
    static Http http;
    if (!httpOpen(&http, method, path, 0)) return false;
    if (http.length >= capacity) {
        strcpy(lastError, "Server response is too large"); httpClose(&http); return false;
    }
    uint32_t used = 0;
    while (used < http.length) {
        int n = httpRead(&http, output + used, http.length - used);
        if (n <= 0) {
            if (!lastError[0]) strcpy(lastError, "Incomplete server response");
            httpClose(&http); return false;
        }
        used += n;
    }
    output[used] = 0;
    httpClose(&http);
    if (http.status != 200) {
        char *message = strchr(output, '\t');
        snprintf(lastError, 192, "HTTP %d: %.155s", http.status, message ? message + 1 : output);
        char *newline = strchr(lastError, '\n');
        if (newline) *newline = 0;
        return false;
    }
    return true;
}
