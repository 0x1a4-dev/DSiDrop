#include "client.h"
#include <nds.h>
#include <dswifi9.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

static uint64_t networkTicks, peerReady, responseReady;
static unsigned packets, operations, responseOffset, responseSize, socketCount, closeCount, registrationCount;
static unsigned peerDelayMs, responseDelayMs;
static int replyStatus;
static bool holdEof, registrationOk;
static char fakeResponse[800], lastPoll[1400], lastRegistration[640];
static const char *replyBody;
static char checkpoint[1024];
static EnvExtraInfo fakeEnvironment;
static const char noTask[] = "RFCLIENT1\t3000\t-\t-\t-\t-\t0\n";
static const char assignedTask[] = "RFCLIENT1\t3000\t0123456789abcdef0123456789abcdef\tothers\taaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\tHomebrew\t16384\n";

static uint64_t afterMs(unsigned milliseconds) { return networkTicks + ticksFromUsec((uint64_t)milliseconds * 1000); }
static uint64_t fakeTick(void) { return networkTicks; }
static int fakeAssoc(void) { return ASSOCSTATUS_ASSOCIATED; }
static uint32_t fakeIp(void) { return 0x0100007f; }
static int fakeSocket(int domain, int type, int protocol) { (void)domain; (void)type; (void)protocol; socketCount++; return 7; }
static int fakeIoctl(int fd, int command, ...) { (void)fd; (void)command; return 0; }
static int fakeConnect(int fd, const struct sockaddr *address, socklen_t size) {
    (void)fd; (void)address; (void)size;
    peerReady = afterMs(peerDelayMs);
    errno = EINPROGRESS;
    return -1;
}
static int fakePeer(int fd, struct sockaddr *address, socklen_t *size) {
    (void)fd; (void)address; (void)size;
    if (networkTicks < peerReady) { errno = ENOTCONN; return -1; }
    return 0;
}
static int fakeClose(int fd) { (void)fd; closeCount++; return 0; }
static int fakeStat(const char *path, struct stat *info) { (void)path; (void)info; errno = ENOENT; return -1; }
static int fakeMkdir(const char *path, mode_t mode) { (void)path; (void)mode; return 0; }
static int fakeRemove(const char *path) { (void)path; return 0; }
static int fakeRename(const char *from, const char *to) { (void)from; (void)to; return 0; }
static FILE *fakeFile(const char *path, const char *mode) { (void)path; return *mode == 'w' ? fmemopen(checkpoint, sizeof(checkpoint), "w") : NULL; }
static ssize_t fakeSend(int fd, const void *data, size_t size, int flags) {
    (void)fd; (void)flags; operations++; packets++;
    assert(size < sizeof(lastPoll)); memcpy(lastPoll, data, size); lastPoll[size] = 0;
    responseSize = snprintf(fakeResponse, sizeof(fakeResponse), "HTTP/1.1 %d Test\r\nContent-Length: %u\r\nConnection: close\r\n\r\n%s", replyStatus, (unsigned)strlen(replyBody), replyBody);
    responseOffset = 0; responseReady = afterMs(responseDelayMs);
    return size;
}
static ssize_t fakeRecv(int fd, void *data, size_t size, int flags) {
    (void)fd; (void)flags; operations++;
    if (networkTicks < responseReady || (responseOffset == responseSize && holdEof)) { errno = EAGAIN; return -1; }
    if (responseOffset == responseSize) return 0;
    if (size > 63) size = 63;
    if (size > responseSize - responseOffset) size = responseSize - responseOffset;
    memcpy(data, fakeResponse + responseOffset, size); responseOffset += size; return size;
}
static bool fakeHttpText(const char *method, const char *path, char *output, size_t capacity);

#undef g_envExtraInfo
#define g_envExtraInfo (&fakeEnvironment)
#define tickGetCount fakeTick
#define Wifi_AssocStatus fakeAssoc
#define Wifi_GetIP fakeIp
#define socket(...) fakeSocket(__VA_ARGS__)
#define ioctl fakeIoctl
#define connect fakeConnect
#define getpeername fakePeer
#define send fakeSend
#define recv fakeRecv
#define forceclosesocket fakeClose
#define stat(...) fakeStat(__VA_ARGS__)
#define mkdir fakeMkdir
#define fopen fakeFile
#define rename fakeRename
#define remove fakeRemove
#define httpText fakeHttpText
#define clientConnect testedClientConnect
#define clientRegister testedClientRegister
#define clientRegistered testedClientRegistered
#define clientConnectionStatus testedClientConnectionStatus
#define clientConnectionDetail testedClientConnectionDetail
#define clientMac testedClientMac
#define clientRevision testedClientRevision
#define clientTick testedClientTick
#define clientTake testedClientTake
#define clientPending testedClientPending
#define clientActive testedClientActive
#define clientProgress testedClientProgress
#define clientFinish testedClientFinish
#define clientClose testedClientClose
bool testedClientPending(void);
void testedClientTick(bool mayClaim);
#include "../source/client.c"

static bool fakeHttpText(const char *method, const char *path, char *output, size_t capacity) {
    registrationCount++;
    assert(!strcmp(method, "POST"));
    assert(strstr(path, "/api/dsi/client/poll?mac=001122334455") && strstr(path, "state=busy"));
    snprintf(lastRegistration, sizeof(lastRegistration), "%s", path);

    unsigned before = socketCount;
    assert(!client.enabled);
    testedClientTick(false);
    assert(socketCount == before);
    if (!registrationOk) { strcpy(lastError, "HTTP 404: Client endpoint unavailable"); return false; }
    assert(strlen(replyBody) < capacity);
    strcpy(output, replyBody);
    return true;
}

static void networkFrames(unsigned frames, bool mayClaim) {
    for (unsigned i = 0; i < frames; i++) {
        networkTicks += ticksFromUsec(16667);
        unsigned before = operations; testedClientTick(mayClaim); assert(operations - before <= 1);
    }
}

static void resetNetwork(bool identity) {
    testedClientClose();
    networkTicks = peerReady = responseReady = 0;
    packets = operations = responseOffset = responseSize = socketCount = closeCount = registrationCount = 0;
    peerDelayMs = responseDelayMs = 0; replyStatus = 200;
    holdEof = registrationOk = true; replyBody = noTask;
    memset(&fakeEnvironment, 0, sizeof(fakeEnvironment));
    if (identity) {
        static const uint8_t mac[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
        memcpy(fakeEnvironment.wlmgr_macaddr, mac, sizeof(mac));
    }
    lastError[0] = lastPoll[0] = lastRegistration[0] = 0;
    testedClientConnect();
}

static void awaitRegistration(unsigned frames) {
    while (frames-- && !testedClientRegistered()) networkFrames(1, false);
    assert(testedClientRegistered());
}

int runClientNetworkTests(void) {
    resetNetwork(true);
    assert(!testedClientRegistered() && !strcmp(testedClientMac(), "001122334455"));
    assert(testedClientRegister());
    assert(testedClientRegistered() && registrationCount == 1 && !socketCount);
    assert(strstr(lastRegistration, "version=" APP_VERSION) && *testedClientConnectionStatus());
    puts("Registration: actual initialization and foreground ACK without nested polling passed");

    resetNetwork(false);
    networkFrames(240, false);
    assert(!testedClientRegistered() && !socketCount);
    static const uint8_t recoveredMac[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
    memcpy(fakeEnvironment.wlmgr_macaddr, recoveredMac, sizeof(recoveredMac));
    awaitRegistration(180);
    assert(strstr(lastPoll, "mac=001122334455"));
    puts("Registration: temporarily missing firmware MAC is retried instead of silently disabling the client");

    resetNetwork(true);
    peerDelayMs = 5000; responseDelayMs = 5000;
    awaitRegistration(850);
    assert(networkTicks > ticksFromUsec(10000000) && packets == 1 && closeCount == 1);

    assert(responseOffset == responseSize && holdEof);
    puts("Transport: delayed EINPROGRESS connection, slow response beyond 3 s and Content-Length completion without EOF passed");

    resetNetwork(true);
    responseDelayMs = 20000;
    networkFrames(600, false);
    assert(!testedClientRegistered() && packets == 1 && !closeCount);
    networkFrames(370, false);
    assert(!testedClientRegistered() && closeCount >= 1 && *testedClientConnectionDetail());
    responseDelayMs = 0;
    awaitRegistration(1200);
    puts("Transport: stalled response times out after 15 s and a later retry recovers");

    resetNetwork(true);
    registrationOk = false;
    assert(!testedClientRegister() && !testedClientRegistered());
    assert(strstr(testedClientConnectionDetail(), "404"));
    registrationOk = true;
    awaitRegistration(240);
    resetNetwork(true);
    replyStatus = 404; replyBody = "RFERROR1\tUpdate the PC server\n";
    networkFrames(25, false);
    assert(!testedClientRegistered() && strstr(testedClientConnectionDetail(), "404"));
    replyStatus = 200; replyBody = noTask;
    awaitRegistration(240);
    resetNetwork(true);
    replyBody = "Not a DSiDrop response\n";
    networkFrames(25, false);
    assert(!testedClientRegistered() && *testedClientConnectionDetail());
    replyBody = noTask;
    awaitRegistration(240);
    puts("Registration: startup/background HTTP 404 and malformed replies remain visible and recover on retry");

    resetNetwork(true);
    replyBody = assignedTask;
    networkFrames(25, true);
    assert(packets == 1 && strstr(lastPoll, "mac=001122334455") && strstr(lastPoll, "state=idle"));
    assert(testedClientPending());
    Game game; assert(testedClientTake(&game));
    assert(!testedClientTake(&game)); assert(testedClientActive());
    assert(game.system == SYSTEM_OTHERS);
    testedClientProgress(8192, 16384, "Downloading to SD");
    networkFrames(180, false);
    assert(packets == 2);
    assert(strstr(lastPoll, "state=downloading") && strstr(lastPoll, "received=8192") && strstr(lastPoll, "size=16384"));
    assert(testedClientActive());
    testedClientFinish(true, false, "Saved to SD");
    assert(!testedClientActive() && !testedClientPending());

    networkFrames(4, false); disconnect(); client.due = 0;
    replyBody = noTask;
    networkFrames(30, false);
    assert(strstr(lastPoll, "state=done")); assert(!*client.task.task);
    networkFrames(180, false); assert(strstr(lastPoll, "state=busy"));
    testedClientClose();
    puts("Client network: bounded fragmented IO, heartbeat clock, single dispatch, progress and replayed durable completion ACK passed");
    return 0;
}
