#include "cover.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define COVER_CACHE_SIZE (PAGE_SIZE + 2)
#define COVER_TIMEOUT_FRAMES (60 * 6)

typedef struct {
    char id[33];
    RomSystem system;
    bool available;
    uint32_t used;
    uint32_t revision;
    uint16_t pixels[COVER_PIXELS];
} CachedCover;
typedef struct {
    char id[33];
    RomSystem system;
    bool attempted;
} PageCover;
typedef enum { PREFETCH_IDLE, PREFETCH_CONNECT, PREFETCH_SEND, PREFETCH_HEADERS, PREFETCH_BODY } PrefetchState;
static CachedCover cache[COVER_CACHE_SIZE] __attribute__((aligned(32)));
static PageCover currentPage[PAGE_SIZE];
static unsigned pageCount, activeItem, sendLength, sent, received, idleFrames, totalFrames;
static PrefetchState prefetchState;
static char prefetchHeaders[320];
static uint32_t clock;
static uint32_t contentRevision;
static Http request = { .socket = -1 };
static unsigned char payload[12 + COVER_PIXELS * 2];

static CachedCover *findCached(const char *id, RomSystem system) {
    for (unsigned i = 0; i < COVER_CACHE_SIZE; i++)
        if (cache[i].used && cache[i].system == system && !strcmp(cache[i].id, id)) return &cache[i];
    return NULL;
}

static void remember(const char *id, RomSystem system, bool available) {
    CachedCover *entry = findCached(id, system);
    if (!entry) {
        unsigned oldest = 0;
        for (unsigned i = 1; i < COVER_CACHE_SIZE; i++) if (cache[i].used < cache[oldest].used) oldest = i;
        entry = &cache[oldest];
    }
    strcpy(entry->id, id); entry->system = system;
    entry->available = available; entry->used = ++clock;
    entry->revision = ++contentRevision;
    if (available) memcpy(entry->pixels, payload + 12, sizeof(entry->pixels));
}

static bool validPayload(void) {
    return !memcmp(payload, "RFCOVER2", 8) && payload[8] == COVER_SIZE && !payload[9] &&
        payload[10] == COVER_SIZE && !payload[11];
}

#ifdef ROMSFINDER_UI_TEST
void coverTestFixture(const Game *game) {
    static unsigned next;
    CachedCover *entry = &cache[next++ % COVER_CACHE_SIZE];
    strcpy(entry->id, game->id); entry->system = game->system;
    entry->available = true; entry->used = ++clock;
    entry->revision = ++contentRevision;
    for (unsigned y = 0; y < COVER_SIZE; y++) for (unsigned x = 0; x < COVER_SIZE; x++)
        entry->pixels[y * COVER_SIZE + x] = 0x8000 | (x * 31 / (COVER_SIZE - 1)) |
#ifdef ROMSFINDER_MOTION_TEST
            ((y * 31 / (COVER_SIZE - 1)) << 5) | (((next * 7) % 24 + 7) << 10);
#else
            ((y * 31 / (COVER_SIZE - 1)) << 5) | (24 << 10);
#endif
}

void coverTestMissingFixture(const Game *game) {
    coverTestFixture(game);
    CachedCover *entry = findCached(game->id, game->system);
    if (entry) entry->available = false;
}
#endif

const uint16_t *coverFind(const Game *game, bool *known) {
    CachedCover *entry = findCached(game->id, game->system);
    *known = entry != NULL;
    if (!entry) return NULL;
    entry->used = ++clock;
    return entry->available ? entry->pixels : NULL;
}

uint32_t coverRevision(const uint16_t *pixels) {
    if (!pixels) return 0;
    for (unsigned i = 0; i < COVER_CACHE_SIZE; i++)
        if (pixels == cache[i].pixels) return cache[i].revision;
    return 0;
}

void coverPausePrefetch(void) {
    if (prefetchState == PREFETCH_IDLE) return;
    httpClose(&request);

    currentPage[activeItem].attempted = false;
    prefetchState = PREFETCH_IDLE;
}

void coverPage(const Game *games, unsigned count, unsigned selected) {
    (void)selected;
    unsigned nextCount = count > PAGE_SIZE ? PAGE_SIZE : count;
    bool unchanged = nextCount == pageCount;
    for (unsigned index = 0; unchanged && index < nextCount; index++)
        unchanged = currentPage[index].system == games[index].system && !strcmp(currentPage[index].id, games[index].id);
    if (unchanged && prefetchState != PREFETCH_IDLE) return;
    coverPausePrefetch();
    pageCount = nextCount;
    memset(currentPage, 0, sizeof(currentPage));
    for (unsigned i = 0; i < pageCount; i++) {
        strcpy(currentPage[i].id, games[i].id);
        currentPage[i].system = games[i].system;

        CachedCover *entry = findCached(games[i].id, games[i].system);
        if (entry) entry->used = ++clock;
    }
}

static bool transient(void) {
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS || errno == EALREADY;
}

static void finishPrefetch(void) {
    httpClose(&request);
    prefetchState = PREFETCH_IDLE;
}

static void beginPrefetch(unsigned item) {
    activeItem = item;
    currentPage[item].attempted = true;
    memset(&request, 0, sizeof(request));
    request.socket = -1;
    sent = received = idleFrames = totalFrames = 0;
    struct sockaddr_in server = {0};
    server.sin_family = AF_INET;
    server.sin_port = htons(settings.port);
    if (!inet_aton(settings.host, &server.sin_addr)) return;
    request.socket = socket(AF_INET, SOCK_STREAM, 0);
    if (request.socket < 0) return;
    unsigned long nonblock = 1;
    if (ioctl(request.socket, FIONBIO, &nonblock) < 0) { finishPrefetch(); return; }
    int length = snprintf(prefetchHeaders, sizeof(prefetchHeaders),
        "GET /api/dsi/cover?system=%s&id=%s&size=%u HTTP/1.1\r\nHost: %s:%u\r\n"
        "User-Agent: " APP_NAME "/" APP_VERSION "\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
        systemKey(currentPage[item].system), currentPage[item].id, COVER_SIZE, settings.host, settings.port);
    if (length < 0 || length >= (int)sizeof(prefetchHeaders)) { finishPrefetch(); return; }
    sendLength = (unsigned)length;
    int result = connect(request.socket, (struct sockaddr *)&server, sizeof(server));
    if (result < 0 && !transient()) { finishPrefetch(); return; }
    prefetchState = result == 0 ? PREFETCH_SEND : PREFETCH_CONNECT;
}

static bool parseHeaders(unsigned *bodyStart) {
    request.buffer[request.used] = 0;
    char *end = strstr((char *)request.buffer, "\r\n\r\n");
    if (!end) return false;
    *bodyStart = (unsigned)(end + 4 - (char *)request.buffer);
    *end = 0;
    if (sscanf((char *)request.buffer, "HTTP/%*u.%*u %d", &request.status) != 1) {
        finishPrefetch(); return false;
    }

    if (request.status == 404) {
        remember(currentPage[activeItem].id, currentPage[activeItem].system, false);
        finishPrefetch(); return true;
    }
    bool hasLength = false;
    char *line = strstr((char *)request.buffer, "\r\n");
    while (line) {
        line += 2;
        char *next = strstr(line, "\r\n");
        if (next) *next = 0;
        if (!strncasecmp(line, "Content-Length:", 15)) {
            char *numberEnd;
            errno = 0;
            unsigned long length = strtoul(line + 15, &numberEnd, 10);
            while (*numberEnd == ' ') numberEnd++;
            if (errno || *numberEnd || (request.status == 200 && length != sizeof(payload))) { finishPrefetch(); return false; }
            request.length = (uint32_t)length; hasLength = true;
        }
        if (!strncasecmp(line, "Transfer-Encoding:", 18) ||
            (!strncasecmp(line, "Content-Encoding:", 17) && strcasecmp(line + 18, "identity"))) {
            finishPrefetch(); return false;
        }
        line = next;
    }
    if (request.status == 404 && hasLength) {
        remember(currentPage[activeItem].id, currentPage[activeItem].system, false);
        finishPrefetch(); return true;
    }
    if (request.status != 200 || !hasLength) { finishPrefetch(); return false; }
    prefetchState = PREFETCH_BODY;
    return true;
}

bool coverPrefetchTick(unsigned selected) {
    if (prefetchState == PREFETCH_IDLE) {
        unsigned next = pageCount;

        if (selected < pageCount && !currentPage[selected].attempted &&
            !findCached(currentPage[selected].id, currentPage[selected].system)) next = selected;
        else for (unsigned i = 0; i < pageCount; i++) {
            if (!currentPage[i].attempted && !findCached(currentPage[i].id, currentPage[i].system)) { next = i; break; }
        }
        if (next < pageCount) beginPrefetch(next);
        return false;
    }
    if (++idleFrames > COVER_TIMEOUT_FRAMES || ++totalFrames > COVER_TIMEOUT_FRAMES * 2) {
        finishPrefetch(); return false;
    }
    if (prefetchState == PREFETCH_CONNECT) {
        struct sockaddr_in peer;
        socklen_t length = sizeof(peer);
        if (getpeername(request.socket, (struct sockaddr *)&peer, &length) == 0) {
            prefetchState = PREFETCH_SEND; idleFrames = 0;
        }
        return false;
    }
    if (prefetchState == PREFETCH_SEND) {
        int n = send(request.socket, prefetchHeaders + sent, sendLength - sent, 0);
        if (n > 0) { sent += n; idleFrames = 0; }
        else if (n == 0 || !transient()) { finishPrefetch(); return false; }
        if (sent == sendLength) prefetchState = PREFETCH_HEADERS;
        return false;
    }
    if (prefetchState == PREFETCH_HEADERS) {
        if (request.used == sizeof(request.buffer) - 1) { finishPrefetch(); return false; }
        int n = recv(request.socket, request.buffer + request.used, sizeof(request.buffer) - 1 - request.used, 0);
        if (n < 0 && transient()) return false;
        if (n <= 0) { finishPrefetch(); return false; }
        idleFrames = 0; request.used += n;
        unsigned bodyStart = 0;
        if (!parseHeaders(&bodyStart)) return false;
        if (prefetchState == PREFETCH_IDLE) return true;
        received = request.used - bodyStart;
        if (received > sizeof(payload)) { finishPrefetch(); return false; }
        memcpy(payload, request.buffer + bodyStart, received);

        return false;
    }
    if (received < sizeof(payload)) {
        int n = recv(request.socket, payload + received, sizeof(payload) - received, 0);
        if (n < 0 && transient()) return false;
        if (n <= 0) { finishPrefetch(); return false; }
        received += n; idleFrames = 0;
    }
    if (received != sizeof(payload)) return false;
    bool valid = validPayload();
    if (valid) remember(currentPage[activeItem].id, currentPage[activeItem].system, true);
    finishPrefetch();
    return valid;
}

bool coverLoad(const Game *game) {
    bool known;
    coverFind(game, &known);
    if (known) return true;
    coverPausePrefetch();
    char path[128];
    snprintf(path, sizeof(path), "/api/dsi/cover?system=%s&id=%s&size=%u", systemKey(game->system), game->id, COVER_SIZE);
    if (!httpOpenTimeout(&request, "GET", path, 0, 60 * 5)) return false;
    bool available = false, definitive = request.status == 404;
    if (request.status == 200 && request.length == sizeof(payload)) {
        unsigned receivedBytes = 0;
        while (receivedBytes < sizeof(payload)) {
            int n = httpRead(&request, payload + receivedBytes, sizeof(payload) - receivedBytes);
            if (n <= 0) { httpClose(&request); return false; }
            receivedBytes += n;
        }
        available = validPayload();
        definitive = available;
    }
    httpClose(&request);
    if (definitive) remember(game->id, game->system, available);
    return true;
}

