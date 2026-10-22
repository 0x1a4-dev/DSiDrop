#include "app.h"
#include <assert.h>
#include <string.h>

static DownloadQueue pending, loaded;
static unsigned calls, saved, failAt;
static Game order[QUEUE_CAPACITY];
static bool failSave;

static bool download(const Game *game, void *context) {
    (void)context;
    order[calls++] = *game;
    return calls != failAt;
}

static bool checkpoint(const DownloadQueue *queue, void *context) {
    (void)context;
    saved++;
    assert(queue->count <= QUEUE_CAPACITY);
    return !failSave;
}

static Game game(RomSystem system, unsigned number) {
    Game g = { .system = system, .size = 16384 };
    if (system == SYSTEM_GBA) snprintf(g.id, sizeof(g.id), "%08x", number);
    else snprintf(g.id, sizeof(g.id), "%032x", number);
    snprintf(g.title, sizeof(g.title), "Homebrew %u", number);
    strcpy(g.region, "Europe"); strcpy(g.languages, "En,De");
    return g;
}

int main(void) {
    Game nds = game(SYSTEM_NDS, 1), gba = game(SYSTEM_GBA, 2), other = game(SYSTEM_OTHERS, 3);
    assert(queueAdd(&pending, &nds)); assert(queueAdd(&pending, &gba)); assert(queueAdd(&pending, &other));
    assert(!queueAdd(&pending, &gba) && pending.count == 3);
    FILE *file = tmpfile(); assert(file);
    assert(queueWrite(file, &pending)); rewind(file);
    assert(queueRead(file, &loaded)); fclose(file);
    assert(loaded.count == 3 && loaded.items[1].system == SYSTEM_GBA && !strcmp(loaded.items[1].id, gba.id));
    assert(loaded.items[2].system == SYSTEM_OTHERS && !strcmp(loaded.items[2].id, other.id));
    assert(!strcmp(downloadDirectory(loaded.items[2].system), "/roms/others"));
    assert(!strcmp(downloadDirectory(loaded.items[1].system), "/roms/gba"));
    unsigned complete;
    failAt = 2;
    assert(!queueRun(&loaded, download, checkpoint, NULL, &complete));
    assert(calls == 2 && saved == 1 && complete == 1 && loaded.count == 2);
    assert(order[0].system == SYSTEM_NDS && order[1].system == SYSTEM_GBA);
    assert(!strcmp(loaded.items[0].id, gba.id));
    file = tmpfile(); assert(file); assert(queueWrite(file, &loaded)); rewind(file);
    assert(queueRead(file, &pending)); fclose(file);
    calls = saved = failAt = 0;
    assert(queueRun(&pending, download, checkpoint, NULL, &complete));
    assert(complete == 2 && saved == 2 && calls == 2 && pending.count == 0);
    assert(order[0].system == SYSTEM_GBA && order[1].system == SYSTEM_OTHERS && !strcmp(order[1].id, other.id));

    Game sameIdUpload = nds; sameIdUpload.system = SYSTEM_OTHERS;
    assert(queueAdd(&pending, &nds)); assert(queueAdd(&pending, &sameIdUpload));
    assert(pending.count == 2); queueRemove(&pending, 1); queueRemove(&pending, 0);
    assert(queueAdd(&pending, &nds)); assert(queueAdd(&pending, &gba));
    queueRemove(&pending, 0); assert(pending.count == 1 && pending.items[0].system == SYSTEM_GBA);
    failSave = true; calls = saved = 0;
    assert(!queueRun(&pending, download, checkpoint, NULL, &complete));
    assert(complete == 1 && pending.count == 0 && saved == 1);
    for (unsigned i = 0; i < QUEUE_CAPACITY; i++) { Game g = game(SYSTEM_NDS, i + 100); assert(queueAdd(&pending, &g)); }
    assert(!queueAdd(&pending, &nds));
    file = tmpfile(); assert(file); fputs("RFQUEUE1\t1\ngba\t../../bad\tBad\tEurope\t16384\tEn\n", file); rewind(file);
    assert(!queueRead(file, &loaded) && loaded.count == 0); fclose(file);
    file = tmpfile(); assert(file); fputs("RFQUEUE1\t1\ngba\t12345678\tValid\tEurope\t16384\tEn\tExtra\n", file); rewind(file);
    assert(!queueRead(file, &loaded) && loaded.count == 0); fclose(file);
    file = tmpfile(); assert(file); fputs("RFQUEUE1\t2\ngba\t12345678\tValid\tEurope\t16384\tEn\ngba\t12345678\tDuplicate\tEurope\t16384\tEn\n", file); rewind(file);
    assert(!queueRead(file, &loaded) && loaded.count == 0); fclose(file);
    file = tmpfile(); assert(file); fputs("RFQUEUE1\t2\ngba\t12345678\tTruncated\tEurope\t16384\tEn\n", file); rewind(file);
    assert(!queueRead(file, &loaded) && loaded.count == 0); fclose(file);
    puts("C queue: mixed NDS/GBA/Others order, pause/retry, persistence, scoped duplicates, removal and failures passed");
    return 0;
}
