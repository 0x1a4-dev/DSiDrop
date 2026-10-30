#include "app.h"
#include <nds.h>
#include <dswifi9.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

Settings settings = { .host = "127.0.0.1", .port = 3000 };
char lastError[192];
static uint64_t ticks, yieldTicks, sleepTicks, peerReady, responseReady, sendReady, responseGap, sendGap;
static unsigned yields, sleeps, polls, reads, closes, sends;
static size_t responseSize, responseOffset, chunkSize, sendChunk;
static bool cancelPoll, cancelYield, cancelAfterSleep;
static int receiveError;
static char response[4096];
static char outbound[8192];
static size_t outboundSize;

static uint64_t fakeTick(void) { return ticks; }
bool appPoll(void) {
    polls++;
    if (cancelPoll) strcpy(lastError, "Paused");
    return !cancelPoll;
}
bool appYield(void) {
    yields++; ticks += yieldTicks;
    if (cancelYield) strcpy(lastError, "Paused");
    return !cancelYield;
}
void threadSleep(unsigned usec) {
    assert(usec == 1000);
    sleeps++; ticks += sleepTicks;
    if (cancelAfterSleep) cancelPoll = true;
}
static int fakeSocket(int domain, int type, int protocol) { (void)domain; (void)type; (void)protocol; return 7; }
static int fakeIoctl(int fd, int command, ...) { (void)fd; (void)command; return 0; }
static int fakeConnect(int fd, const struct sockaddr *address, socklen_t size) {
    (void)fd; (void)address; (void)size;
    errno = EINPROGRESS; return -1;
}
static int fakePeer(int fd, struct sockaddr *address, socklen_t *size) {
    (void)fd; (void)address; (void)size;
    if (ticks < peerReady) { errno = ENOTCONN; return -1; }
    return 0;
}
static int fakeClose(int fd) { (void)fd; closes++; return 0; }
static ssize_t fakeSend(int fd, const void *data, size_t size, int flags) {
    (void)fd; (void)data; (void)flags; sends++;
    if (ticks < sendReady) { errno = EAGAIN; return -1; }
    if (size > sendChunk) size = sendChunk;
    assert(outboundSize + size < sizeof(outbound));
    memcpy(outbound + outboundSize, data, size); outboundSize += size;
    sendReady = ticks + sendGap;
    return size;
}
static ssize_t fakeRecv(int fd, void *data, size_t size, int flags) {
    (void)fd; (void)flags; reads++;
    if (receiveError) { errno = receiveError; return -1; }
    if (ticks < responseReady) { errno = EAGAIN; return -1; }
    if (size > chunkSize) size = chunkSize;
    if (size > responseSize - responseOffset) size = responseSize - responseOffset;
    memcpy(data, response + responseOffset, size); responseOffset += size;
    responseReady = ticks + responseGap;
    return size;
}

#define tickGetCount fakeTick
#define socket(...) fakeSocket(__VA_ARGS__)
#define ioctl fakeIoctl
#define connect fakeConnect
#define getpeername fakePeer
#define forceclosesocket fakeClose
#define send fakeSend
#define recv fakeRecv
#include "../source/http.c"

static void reset(void) {
    ticks = peerReady = responseReady = sendReady = responseGap = sendGap = 0;
    yieldTicks = 16667; sleepTicks = 1000;
    yields = sleeps = polls = reads = closes = sends = 0;
    responseOffset = 0; chunkSize = sendChunk = sizeof(response);
    cancelPoll = cancelYield = cancelAfterSleep = false; receiveError = 0;
    lastError[0] = 0;
    memset(outbound, 0, sizeof(outbound)); outboundSize = 0;
    responseSize = snprintf(response, sizeof(response),
        "HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nhomebrew!");
}

int main(void) {
    Http http;
    char data[256];

    reset();
    http = (Http){ .socket = 7, .timeoutFrames = 60 };
    memset(response, 'x', sizeof(response)); responseSize = sizeof(response); chunkSize = 4;
    for (unsigned block = 0; block < sizeof(response) / chunkSize; block++) {
        assert(httpRead(&http, data, sizeof(data)) == 4);
        assert(!memcmp(data, "xxxx", 4));
    }
    assert(reads == 1024 && polls == 1024 && !yields && !sleeps && !ticks);
    assert(httpRead(&http, data, sizeof(data)) == 0 && !yields && !sleeps);
    puts("HTTP: 1024 immediately available blocks and EOF without a waiting frame passed");

    reset();
    assert(httpOpen(&http, "GET", "/api/dsi/file", 0));
    assert(http.status == 200 && http.length == 9 && !yields);
    unsigned before = reads;
    assert(httpRead(&http, data, 4) == 4 && !memcmp(data, "home", 4));
    assert(httpRead(&http, data, sizeof(data)) == 5 && !memcmp(data, "brew!", 5));
    assert(reads == before && !yields);
    httpClose(&http); assert(closes == 1 && http.socket == -1);
    puts("HTTP: headers, buffered body and partial reads remain intact");

    reset(); chunkSize = 1;
    assert(httpOpen(&http, "GET", "/api/dsi/file", 0) && !yields);
    for (unsigned index = 0; index < 9; index++) {
        assert(httpRead(&http, data, sizeof(data)) == 1 && data[0] == "homebrew!"[index]);
    }
    httpClose(&http);

    reset(); responseReady = 50000;
    http = (Http){ .socket = 7, .timeoutFrames = 60 };
    assert(httpRead(&http, data, sizeof(data)) > 0);
    assert(!yields && sleeps == 50 && ticks == responseReady);

    reset(); responseReady = 1500;
    assert(httpRead(&http, data, sizeof(data)) > 0);
    assert(!yields && sleeps == 2 && ticks == 2000);
    puts("HTTP: unavailable data sleeps for 1 ms, resumes before VBlank and never busy-spins");

    reset(); responseReady = 2000000; sleepTicks = 400000;
    http = (Http){ .socket = 7, .timeoutFrames = 60 };
    assert(httpRead(&http, data, sizeof(data)) == -1);
    assert(!yields && sleeps == 3 && ticks == 1200000 && strstr(lastError, "timed out"));
    puts("HTTP: waiting only for unavailable data and real elapsed receive timeout passed");

    reset(); responseGap = sleepTicks = 400000; chunkSize = 2;
    http = (Http){ .socket = 7, .timeoutFrames = 60 };
    for (unsigned block = 0; block < 5; block++) assert(httpRead(&http, data, sizeof(data)) == 2);
    assert(ticks == 1600000 && !*lastError);

    reset(); cancelPoll = true;
    http = (Http){ .socket = 7, .timeoutFrames = 60 };
    assert(httpRead(&http, data, sizeof(data)) == -1 && !reads && !yields && !sleeps);
    assert(!strcmp(lastError, "Paused"));
    reset(); responseReady = 2000000; cancelAfterSleep = true;
    assert(httpRead(&http, data, sizeof(data)) == -1 && sleeps == 1 && !yields && reads == 1);
    assert(!strcmp(lastError, "Paused"));
    reset(); receiveError = ECONNRESET;
    assert(httpRead(&http, data, sizeof(data)) == -1 && !yields && !sleeps);
    assert(strstr(lastError, "Network error"));
    puts("HTTP: idle timeout resets after data, busy/idle cancellation and socket errors passed");

    reset(); peerReady = 2000000; yieldTicks = 400000;
    assert(!httpOpenTimeout(&http, "GET", "/api/dsi/file", 0, 60));
    assert(yields == 3 && closes == 1 && strstr(lastError, "unreachable"));
    reset(); sendReady = 2000000; yieldTicks = 400000;
    assert(!httpOpenTimeout(&http, "GET", "/api/dsi/file", 0, 60));
    assert(yields == 3 && closes == 1 && strstr(lastError, "request timed out"));
    reset(); peerReady = 400000; sendChunk = 16; sendGap = 600000; yieldTicks = 400000;
    assert(httpOpenTimeout(&http, "GET", "/api/dsi/file", 0, 60));
    assert(ticks > 1000000 && sends > 1 && !*lastError);
    httpClose(&http);
    puts("HTTP: elapsed connect/send timeouts and deadline reset after send progress passed");
    reset(); sendChunk = 2;
    FILE *save = tmpfile(); assert(save);
    assert(fwrite("backup!", 1, 7, save) == 7); rewind(save);
    assert(httpUpload("/api/saves/sync", save, 7));
    assert(strstr(outbound, "PUT /api/saves/sync HTTP/1.1\r\n"));
    assert(strstr(outbound, "Content-Length: 7\r\n"));
    char *body = strstr(outbound, "\r\n\r\n"); assert(body);
    assert(outboundSize == (size_t)(body + 4 - outbound) + 7 && !memcmp(body + 4, "backup!", 7));
    reset(); rewind(save); cancelYield = true;
    assert(!httpUpload("/api/saves/sync", save, 7) && closes == 1);
    reset(); rewind(save);
    assert(!httpUpload("/api/saves/sync", save, 8) && closes == 1 && strstr(lastError, "read save"));
    fclose(save);
    puts("HTTP: save upload content length, fragmented sends, cancellation and truncated SD file passed");
    return 0;
}