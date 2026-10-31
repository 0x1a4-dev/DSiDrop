#include "cover.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

static unsigned operations, requests, wireUsed, wireLength;
static char requested[64][33];
static unsigned char wire[12 + COVER_PIXELS * 2 + 256];
static bool stalled;
static int fakeSocket(int domain, int type, int protocol) { (void)domain; (void)type; (void)protocol; return 7; }
static int fakeIoctl(int fd, int command, ...) { (void)fd; (void)command; return 0; }
static int fakeConnect(int fd, const struct sockaddr *address, socklen_t size) { (void)fd; (void)address; (void)size; return 0; }
static int fakePeer(int fd, struct sockaddr *address, socklen_t *size) { (void)fd; (void)address; (void)size; return 0; }
static void fakeClose(Http *http) { http->socket = -1; }
static bool fakeOpen(Http *http, const char *method, const char *path, uint32_t offset, unsigned timeout) { (void)http; (void)method; (void)path; (void)offset; (void)timeout; return false; }
static int fakeRead(Http *http, void *buffer, size_t length) { (void)http; (void)buffer; (void)length; return -1; }
static ssize_t fakeSend(int fd, const void *buffer, size_t length, int flags) {
    (void)fd; (void)flags; operations++;
    char header[400]; assert(length < sizeof(header)); memcpy(header, buffer, length); header[length] = 0;
    char *id = strstr(header, "&id="); assert(id); id += 4;
    unsigned size = strspn(id, "0123456789abcdef"); assert(size == 32 || size == 8);
    assert(requests < 64); memcpy(requested[requests], id, size); requested[requests++][size] = 0;
    bool missing = id[0] == '4';
    wireLength = snprintf((char *)wire, sizeof(wire), "HTTP/1.1 %s\r\nContent-Length: %u\r\nConnection: close\r\n\r\n", missing ? "404 Not Found" : "200 OK", missing ? 0 : 12 + COVER_PIXELS * 2);
    if (!missing) {
        unsigned char *payload = wire + wireLength;
        memcpy(payload, "RFCOVER2", 8); payload[8] = COVER_SIZE; payload[9] = 0; payload[10] = COVER_SIZE; payload[11] = 0;
        for (unsigned i = 0; i < COVER_PIXELS; i++) { payload[12 + i * 2] = 31; payload[13 + i * 2] = 0x80; }
        wireLength += 12 + COVER_PIXELS * 2;
    }
    wireUsed = 0; return length;
}
static ssize_t fakeRecv(int fd, void *buffer, size_t length, int flags) {
    (void)fd; (void)flags; operations++;
    if (stalled) { errno = EAGAIN; return -1; }
    if (wireUsed == wireLength) return 0;
    if (length > 257) length = 257;
    if (length > wireLength - wireUsed) length = wireLength - wireUsed;
    memcpy(buffer, wire + wireUsed, length); wireUsed += length; return length;
}
#define socket(...) fakeSocket(__VA_ARGS__)
#define ioctl fakeIoctl
#define connect fakeConnect
#define getpeername fakePeer
#define send fakeSend
#define recv fakeRecv
#define httpClose fakeClose
#define httpOpenTimeout fakeOpen
#define httpRead fakeRead
#define coverFind testedCoverFind
#define coverLoad testedCoverLoad
#define coverPage testedCoverPage
#define coverPausePrefetch testedCoverPausePrefetch
#define coverPrefetchTick testedCoverPrefetchTick
#define coverRevision testedCoverRevision
#include "../source/cover.c"

static void ticks(unsigned count, unsigned selected) {
    for (unsigned i = 0; i < count; i++) {
        unsigned before = operations; testedCoverPrefetchTick(selected);
        assert(operations - before <= 1);
    }
}

int runCoverTests(void) {
    static Game page[PAGE_SIZE];
    for (unsigned i = 0; i < PAGE_SIZE; i++) {
        memset(page[i].id, '0' + i, 32); page[i].id[32] = 0;
        page[i].system = SYSTEM_NDS;
    }
    testedCoverPage(page, PAGE_SIZE, 7);
    ticks(3, 7);
    for (unsigned redraw = 0; redraw < 2000; redraw++) {
        testedCoverPage(page, PAGE_SIZE, 7);
        ticks(1, 7);
    }
    assert(requests == PAGE_SIZE && !strcmp(requested[0], page[7].id));
    bool known;
    for (unsigned i = 0; i < PAGE_SIZE; i++) {
        const uint16_t *cover = testedCoverFind(&page[i], &known);
        assert(known && ((i == 4) == (cover == NULL)));
        if (cover) {
            uint32_t revision = testedCoverRevision(cover);
            assert(revision);
            assert(testedCoverFind(&page[i], &known) == cover);
            assert(testedCoverRevision(cover) == revision);
        }
    }
    assert(testedCoverRevision(NULL) == 0);
    ticks(200, 3); assert(requests == PAGE_SIZE);
    static Game other = { .id = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", .system = SYSTEM_OTHERS };
    testedCoverPage(&other, 1, 0); ticks(3, 0);
    assert(requests == PAGE_SIZE + 1);
    testedCoverPage(page, PAGE_SIZE, 0); ticks(100, 0);
    assert(!testedCoverFind(&other, &known) && !known);
    testedCoverPage(&other, 1, 0); ticks(3, 0); testedCoverPausePrefetch(); ticks(100, 0);
    assert(testedCoverFind(&other, &known) && known);
    Game scoped = other; scoped.system = SYSTEM_NDS;
    assert(!testedCoverFind(&scoped, &known) && !known);
    testedCoverPage(&scoped, 1, 0); stalled = true; ticks(800, 0); stalled = false;
    assert(!testedCoverFind(&scoped, &known) && !known);
    testedCoverPage(&scoped, 1, 0); ticks(100, 0);
    assert(testedCoverFind(&scoped, &known) && known);
    testedCoverPausePrefetch();
    const uint16_t *reused = cache[0].pixels;
    uint32_t previousRevision = testedCoverRevision(reused);
    cache[0].used = 0;
    remember("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee", SYSTEM_NDS, false);
    assert(testedCoverRevision(reused) != previousRevision);
    puts("Cover prefetch: all ten, selected first, bounded fragmented I/O, full-page/negative cache, stale cancellation, pause/resume and system isolation passed");
    return 0;
}

#ifdef ROMSFINDER_HOST_TEST
Settings settings = { .host = "127.0.0.1", .port = 3000 };
const char *systemKey(RomSystem system) {
    static const char *keys[] = {"nds", "gba", "others"};
    return keys[system];
}
int main(void) { return runCoverTests(); }
#endif
