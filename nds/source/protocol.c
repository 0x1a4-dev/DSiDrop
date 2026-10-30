#include "app.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>

static char *field(char **cursor, char separator) {
    if (!*cursor) return NULL;
    char *result = *cursor;
    char *end = strchr(result, separator);
    if (end) { *end = 0; *cursor = end + 1; }
    else *cursor = NULL;
    return result;
}

static bool number(const char *s, uint32_t *value, int base) {
    if (!s || !*s || *s == '-') return false;
    char *end;
    errno = 0;
    unsigned long n = strtoul(s, &end, base);
    if (errno || *end || n > UINT32_MAX) return false;
    *value = (uint32_t)n;
    return true;
}

static bool copyField(char *dest, size_t size, const char *source) {
    if (!source || strlen(source) >= size) return false;
    memcpy(dest, source, strlen(source) + 1);
    return true;
}

const char *systemKey(RomSystem system) {
    switch (system) {
        case SYSTEM_NDS: return "nds";
        case SYSTEM_GBA: return "gba";
        case SYSTEM_OTHERS: return "others";
        default: return "";
    }
}

const char *systemLabel(RomSystem system) {
    switch (system) {
        case SYSTEM_NDS: return "NDS";
        case SYSTEM_GBA: return "GBA";
        case SYSTEM_OTHERS: return "Others";
        default: return "Unknown";
    }
}

const char *downloadDirectory(RomSystem system) {
    switch (system) {
        case SYSTEM_NDS: return "/roms/nds";
        case SYSTEM_GBA: return "/roms/gba";
        case SYSTEM_OTHERS: return "/roms/others";
        default: return "";
    }
}

const char *mediaDirectory(const char *name) {
    const char *extension = strrchr(name, '.');
    if (!extension) return NULL;
    static const struct { const char *extension, *directory; } formats[] = {
        { ".mp4", "/videos" }, { ".avi", "/videos" }, { ".mov", "/videos" },
        { ".mkv", "/videos" }, { ".webm", "/videos" }, { ".mpeg", "/videos" },
        { ".mpg", "/videos" }, { ".dpg", "/videos" }, { ".rvid", "/videos" },
        { ".mp3", "/music" }, { ".wav", "/music" }, { ".ogg", "/music" },
        { ".aac", "/music" }, { ".m4a", "/music" }, { ".flac", "/music" },
        { ".mod", "/music" }, { ".it", "/music" }, { ".s3m", "/music" }, { ".xm", "/music" },
        { ".jpg", "/pictures" }, { ".jpeg", "/pictures" }, { ".png", "/pictures" },
        { ".gif", "/pictures" }, { ".bmp", "/pictures" }, { ".webp", "/pictures" },
    };
    for (unsigned index = 0; index < sizeof(formats) / sizeof(formats[0]); index++)
        if (!strcasecmp(extension, formats[index].extension)) return formats[index].directory;
    return NULL;
}

bool gameValid(const Game *game) {
    if ((unsigned)game->system >= SYSTEM_COUNT) return false;
    const char *fields[] = { game->id, game->title, game->region, game->languages };
    const size_t sizes[] = { sizeof(game->id), sizeof(game->title), sizeof(game->region), sizeof(game->languages) };
    for (unsigned i = 0; i < 4; i++) {
        const char *end = memchr(fields[i], 0, sizes[i]);
        if (!end) return false;
        for (const char *c = fields[i]; c < end; c++) if ((unsigned char)*c < 32 || (unsigned char)*c > 126) return false;
    }
    size_t length = game->system == SYSTEM_GBA ? 8 : 32;
    return strlen(game->id) == length && strspn(game->id, "0123456789abcdef") == length &&
        *game->title && game->size <= MAX_GAME_SIZE;
}

bool parseCatalog(char *text, Game *games, int *count, int *total, int *page, int *pages) {
    return parseCatalogForSystem(text, games, count, total, page, pages, SYSTEM_NDS);
}

bool parseCatalogForSystem(char *text, Game *games, int *count, int *total, int *page, int *pages, RomSystem system) {
    if ((unsigned)system >= SYSTEM_COUNT) return false;
    char *lines = text, *header = field(&lines, '\n');
    if (!header) return false;
    char *columns = header;
    const char *magic = field(&columns, '\t');
    uint32_t n, p, ps;
    if (!magic || strcmp(magic, "RFCATALOG1") ||
        !number(field(&columns, '\t'), &n, 10) || n > INT_MAX ||
        !number(field(&columns, '\t'), &p, 10) || p > INT_MAX ||
        !number(field(&columns, '\t'), &ps, 10) || ps > INT_MAX || ps < 1 || p >= ps) return false;
    if (ps != (n ? (n + PAGE_SIZE - 1) / PAGE_SIZE : 1)) return false;
    *total = (int)n; *page = (int)p; *pages = (int)ps; *count = 0;
    char *line;
    while ((line = field(&lines, '\n')) && *line) {
        if (*count == PAGE_SIZE) return false;
        Game *g = &games[(*count)++];
        g->system = system;
        columns = line;
        if (!copyField(g->id, sizeof(g->id), field(&columns, '\t')) ||
            !copyField(g->title, sizeof(g->title), field(&columns, '\t')) ||
            !copyField(g->region, sizeof(g->region), field(&columns, '\t')) ||
            !number(field(&columns, '\t'), &g->size, 10) ||
            !copyField(g->languages, sizeof(g->languages), field(&columns, '\t')) || columns || !gameValid(g)) return false;
    }
    unsigned expected = n - p * PAGE_SIZE;
    if (expected > PAGE_SIZE) expected = PAGE_SIZE;
    return (unsigned)*count == expected;
}

bool parseJob(char *text, Job *job) {
    return parseJobForSystem(text, job, SYSTEM_NDS);
}

bool parseJobForSystem(char *text, Job *job, RomSystem system) {
    if ((unsigned)system >= SYSTEM_COUNT) return false;
    char *end = strchr(text, '\n');
    if (end) *end = 0;
    char *columns = text;
    const char *magic = field(&columns, '\t');
    if (!magic || strcmp(magic, "RFSTATUS1") ||
        !copyField(job->state, sizeof(job->state), field(&columns, '\t')) ||
        !number(field(&columns, '\t'), &job->received, 10) ||
        !number(field(&columns, '\t'), &job->total, 10) ||
        !number(field(&columns, '\t'), &job->size, 10)) return false;
    const char *crc = field(&columns, '\t');
    job->crc = 0;
    if (!crc || (strcmp(crc, "-") && (strlen(crc) != 8 || !number(crc, &job->crc, 16)))) return false;
    if (!copyField(job->name, sizeof(job->name), field(&columns, '\t')) ||
        !copyField(job->message, sizeof(job->message), field(&columns, '\t')) || columns) return false;
    if (strcmp(job->state, "checking") && strcmp(job->state, "downloading") &&
        strcmp(job->state, "extracting") && strcmp(job->state, "ready") && strcmp(job->state, "error")) return false;
    if (!strcmp(job->state, "ready")) {
        size_t length = strlen(job->name);
        bool theme = system == SYSTEM_OTHERS && length > 6 && !strcmp(job->name + length - 6, ".theme");
        bool folder = system == SYSTEM_OTHERS && length > 7 && !strcmp(job->name + length - 7, ".folder");
        bool media = system == SYSTEM_OTHERS && mediaDirectory(job->name) != NULL;
        if (job->size < (media || folder ? 1U : 512U) || job->size > MAX_GAME_SIZE || !strcmp(crc, "-") ||
            length < 5 || (!theme && !folder && !media && strcmp(job->name + length - 4, system == SYSTEM_GBA ? ".gba" : ".nds")) || job->name[0] == '.' ||
            strpbrk(job->name, "/\\:<>\"|?*")) return false;
        for (const char *character = job->name; *character; character++)
            if ((unsigned char)*character < 32 || (unsigned char)*character > 126) return false;
    }
    return true;
}

uint32_t crcUpdate(uint32_t crc, const void *data, size_t length) {

    static uint32_t table[256];
    static bool initialized;
    if (!initialized) {
        for (unsigned i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++) c = (c >> 1) ^ ((c & 1) ? 0xedb88320U : 0);
            table[i] = c;
        }
        initialized = true;
    }
    const unsigned char *bytes = data;
    while (length--) crc = (crc >> 8) ^ table[(crc ^ *bytes++) & 255];
    return crc;
}

uint64_t fileFingerprint(const char *path, uint32_t size, uint32_t modified) {
    uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char *character = (const unsigned char *)path; *character; character++)
        hash = (hash ^ *character) * 1099511628211ULL;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        hash = (hash ^ ((size >> shift) & 255)) * 1099511628211ULL;
        hash = (hash ^ ((modified >> shift) & 255)) * 1099511628211ULL;
    }
    return hash;
}

void urlEncode(const char *source, char *dest, size_t capacity) {
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;
    while (*source && used + 3 < capacity) {
        unsigned char c = (unsigned char)*source++;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') dest[used++] = c;
        else { dest[used++] = '%'; dest[used++] = hex[c >> 4]; dest[used++] = hex[c & 15]; }
    }
    if (capacity) dest[used] = 0;
}
