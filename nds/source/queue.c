#include "app.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

bool queueAdd(DownloadQueue *queue, const Game *game) {
    if (queue->count >= QUEUE_CAPACITY || !gameValid(game)) return false;
    for (unsigned i = 0; i < queue->count; i++)
        if (queue->items[i].system == game->system && !strcmp(queue->items[i].id, game->id)) return false;
    queue->items[queue->count++] = *game;
    return true;
}

void queueRemove(DownloadQueue *queue, unsigned index) {
    if (index >= queue->count) return;
    queue->count--;
    memmove(&queue->items[index], &queue->items[index + 1], (queue->count - index) * sizeof(Game));
}

static bool safeField(const char *value, size_t capacity) {
    const char *end = memchr(value, 0, capacity);
    if (!end) return false;
    for (; value < end; value++) if ((unsigned char)*value < 32 || (unsigned char)*value > 126) return false;
    return true;
}

bool queueWrite(FILE *file, const DownloadQueue *queue) {
    if (queue->count > QUEUE_CAPACITY || fprintf(file, "RFQUEUE1\t%u\n", queue->count) < 0) return false;
    for (unsigned i = 0; i < queue->count; i++) {
        const Game *g = &queue->items[i];
        if (!safeField(g->id, sizeof(g->id)) || !safeField(g->title, sizeof(g->title)) ||
            !safeField(g->region, sizeof(g->region)) || !safeField(g->languages, sizeof(g->languages)) || !gameValid(g)) return false;
        if (fprintf(file, "%s\t%s\t%s\t%s\t%lu\t%s\n", systemKey(g->system), g->id,
            g->title, g->region, (unsigned long)g->size, g->languages) < 0) return false;
    }
    return !ferror(file);
}

bool queueRead(FILE *file, DownloadQueue *queue) {
    char line[256], catalog[320];
    Game game;
    queue->count = 0;
    if (!fgets(line, sizeof(line), file) || strncmp(line, "RFQUEUE1\t", 9)) return false;
    char *end;
    errno = 0;
    unsigned long expected = strtoul(line + 9, &end, 10);
    if (errno || end == line + 9 || strcmp(end, "\n") || expected > QUEUE_CAPACITY) return false;
    while (fgets(line, sizeof(line), file)) {
        if (!strchr(line, '\n')) goto invalid;
        char *entry = strchr(line, '\t');
        if (!entry) goto invalid;
        *entry++ = 0;
        RomSystem system;
        if (!strcmp(line, "nds")) system = SYSTEM_NDS;
        else if (!strcmp(line, "gba")) system = SYSTEM_GBA;
        else if (!strcmp(line, "others")) system = SYSTEM_OTHERS;
        else goto invalid;
        snprintf(catalog, sizeof(catalog), "RFCATALOG1\t1\t0\t1\n%s", entry);
        int count, total, page, pages;

        if (!parseCatalogForSystem(catalog, &game, &count, &total, &page, &pages, system) ||
            !queueAdd(queue, &game)) goto invalid;
    }
    if (ferror(file) || queue->count != expected) goto invalid;
    return true;
invalid:
    queue->count = 0;
    return false;
}

bool queueRun(DownloadQueue *queue, QueueTransfer transfer, QueueCheckpoint checkpoint, void *context, unsigned *completed) {
    *completed = 0;
    while (queue->count) {
        if (!transfer(&queue->items[0], context)) return false;
        queueRemove(queue, 0);
        (*completed)++;
        if (!checkpoint(queue, context)) return false;
    }
    return true;
}
