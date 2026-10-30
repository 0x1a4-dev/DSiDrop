#include "app.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool job(const char *text) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "%s", text);
    Job result;
    return parseJob(buffer, &result);
}

int main(int argc, char **argv) {
    uint64_t fingerprint = fileFingerprint("/roms/Game.nds", 1024, 123);
    assert(fingerprint == fileFingerprint("/roms/Game.nds", 1024, 123));
    assert(fingerprint != fileFingerprint("/roms/Renamed.nds", 1024, 123));
    assert(fingerprint != fileFingerprint("/roms/Game.nds", 1025, 123));
    assert(fingerprint != fileFingerprint("/roms/Game.nds", 1024, 124));
    assert((crcUpdate(0xffffffffU, "123456789", 9) ^ 0xffffffffU) == 0xcbf43926U);
    uint32_t crc = crcUpdate(0xffffffffU, "1234", 4);
    assert((crcUpdate(crc, "56789", 5) ^ 0xffffffffU) == 0xcbf43926U);
    assert((crcUpdate(0xffffffffU, "", 0) ^ 0xffffffffU) == 0);
    char encoded[64];
    urlEncode("Mario & Luigi / 2", encoded, sizeof(encoded));
    assert(!strcmp(encoded, "Mario%20%26%20Luigi%20%2F%202"));
    assert(job("RFSTATUS1\tready\t0\t0\t8388608\tcbf43926\tGame [12345678].nds\tReady\n"));
    assert(job("RFSTATUS1\tdownloading\t1024\t4000\t0\t-\t-\tPC downloads\n"));
    char gbaStatus[] = "RFSTATUS1\tready\t0\t0\t32768\tcbf43926\tGame.gba\tReady\n";
    Job gbaJob;
    assert(parseJobForSystem(gbaStatus, &gbaJob, SYSTEM_GBA));
    assert(!job("RFSTATUS1\tready\t0\t0\t32768\tcbf43926\tGame.gba\tReady\n"));
    char wrongType[] = "RFSTATUS1\tready\t0\t0\t32768\tcbf43926\tGame.nds\tReady\n";
    assert(!parseJobForSystem(wrongType, &gbaJob, SYSTEM_GBA));
    char uploadStatus[] = "RFSTATUS1\tready\t0\t0\t32768\tcbf43926\tMy Homebrew.nds\tReady\n";
    assert(parseJobForSystem(uploadStatus, &gbaJob, SYSTEM_OTHERS));
    char wrongUploadType[] = "RFSTATUS1\tready\t0\t0\t32768\tcbf43926\tMy Homebrew.gba\tReady\n";
    assert(!parseJobForSystem(wrongUploadType, &gbaJob, SYSTEM_OTHERS));
    const char *mediaNames[] = { "Movie.dpg", "Track.mp3", "Photo.PNG", "Music.it" };
    const char *mediaFolders[] = { "/videos", "/music", "/pictures", "/music" };
    for (unsigned index = 0; index < sizeof(mediaNames) / sizeof(mediaNames[0]); index++) {
        char mediaStatus[256];
        snprintf(mediaStatus, sizeof(mediaStatus), "RFSTATUS1\tready\t0\t0\t1\tcbf43926\t%s\tReady\n", mediaNames[index]);
        assert(!strcmp(mediaDirectory(mediaNames[index]), mediaFolders[index]));
        assert(parseJobForSystem(mediaStatus, &gbaJob, SYSTEM_OTHERS));
        snprintf(mediaStatus, sizeof(mediaStatus), "RFSTATUS1\tready\t0\t0\t512\tcbf43926\t%s\tReady\n", mediaNames[index]);
        assert(!parseJobForSystem(mediaStatus, &gbaJob, SYSTEM_NDS));
    }
    assert(!mediaDirectory("Game.nds") && !mediaDirectory("File.mp3.exe"));
    char unsafeMedia[] = "RFSTATUS1\tready\t0\t0\t100\tcbf43926\t../Photo.png\tReady\n";
    assert(!parseJobForSystem(unsafeMedia, &gbaJob, SYSTEM_OTHERS));
    char emptyMedia[] = "RFSTATUS1\tready\t0\t0\t0\tcbf43926\tPhoto.png\tReady\n";
    assert(!parseJobForSystem(emptyMedia, &gbaJob, SYSTEM_OTHERS));
    assert(!job("RFSTATUS1\tready\t0\t0\t8388608\t-\tGame.nds\tReady\n"));
    assert(!job("RFSTATUS1\tready\t0\t0\t8388608\t12345678\t../Game.nds\tReady\n"));
    assert(!job("RFSTATUS1\tready\t0\t0\t8388608\t12345678\tGame.zip\tReady\n"));
    assert(!job("RFSTATUS1\tready\t0\t0\t536870913\t12345678\tGame.nds\tReady\n"));
    assert(!job("RFSTATUS1\tready\t0\t0\t-1\t12345678\tGame.nds\tReady\n"));
    assert(!job("RFSTATUS1\tready\t0\t0\t512\t12345678\tGame.nds\n"));
    Game games[PAGE_SIZE];
    int count, total, page, pages;
    char valid[] = "RFCATALOG1\t1\t0\t1\n0123456789abcdef0123456789abcdef\tGame\tEurope\t123456\tEn,De\n";
    assert(parseCatalog(valid, games, &count, &total, &page, &pages));
    assert(count == 1 && total == 1 && page == 0 && pages == 1);
    assert(!strcmp(games[0].title, "Game") && games[0].size == 123456);
    char gbaCatalog[] = "RFCATALOG1\t1\t0\t1\ncbf43926\tGBA Homebrew\tEurope\t32768\tEn\n";
    assert(parseCatalogForSystem(gbaCatalog, games, &count, &total, &page, &pages, SYSTEM_GBA));
    assert(games[0].system == SYSTEM_GBA && !strcmp(games[0].id, "cbf43926"));
    char uploadCatalog[] = "RFCATALOG1\t1\t0\t1\n0123456789abcdef0123456789abcdef\tMy Homebrew\tUnknown\t32768\tUnknown\n";
    assert(parseCatalogForSystem(uploadCatalog, games, &count, &total, &page, &pages, SYSTEM_OTHERS));
    assert(games[0].system == SYSTEM_OTHERS && !strcmp(systemKey(games[0].system), "others"));
    assert(!strcmp(downloadDirectory(games[0].system), "/roms/others"));
    char emptyUploads[] = "RFCATALOG1\t0\t0\t1\n";
    assert(parseCatalogForSystem(emptyUploads, games, &count, &total, &page, &pages, SYSTEM_OTHERS) && !count);
    char shortUploadId[] = "RFCATALOG1\t1\t0\t1\ncbf43926\tMy Homebrew\tUnknown\t32768\tUnknown\n";
    assert(!parseCatalogForSystem(shortUploadId, games, &count, &total, &page, &pages, SYSTEM_OTHERS));
    char wrongIdType[] = "RFCATALOG1\t1\t0\t1\ncbf43926\tGBA Homebrew\tEurope\t32768\tEn\n";
    assert(!parseCatalog(wrongIdType, games, &count, &total, &page, &pages));
    char badId[] = "RFCATALOG1\t1\t0\t1\n../file\tGame\tEurope\t123456\tEn\n";
    assert(!parseCatalog(badId, games, &count, &total, &page, &pages));
    char badPage[] = "RFCATALOG1\t1\t99\t1\n";
    assert(!parseCatalog(badPage, games, &count, &total, &page, &pages));
#ifndef ROMSFINDER_PROTOCOL_TEST
    if (argc == 3) {

        char buffer[4096];
        FILE *file = fopen(argv[1], "rb");
        assert(file);
        size_t n = fread(buffer, 1, sizeof(buffer) - 1, file);
        fclose(file); buffer[n] = 0;
        assert(parseCatalog(buffer, games, &count, &total, &page, &pages));
        assert(count > 0);
        file = fopen(argv[2], "rb");
        assert(file);
        n = fread(buffer, 1, sizeof(buffer) - 1, file);
        fclose(file); buffer[n] = 0;
        Job status;
        assert(parseJob(buffer, &status) && !strcmp(status.state, "ready"));
    }
#else
    (void)argc; (void)argv;
#endif
    puts("C protocol: NDS/GBA/Others IDs, extensions, empty uploads, CRC32 and input validation passed");
    return 0;
}
