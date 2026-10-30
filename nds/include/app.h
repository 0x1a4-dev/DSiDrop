#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define APP_NAME "DSiDrop"
#define APP_VERSION "1.19.0"

#define PAGE_SIZE 10
#define MAX_GAME_SIZE (512U * 1024U * 1024U)
#define CONFIG_PATH "/_nds/romsfinder.ini"
#define DEFAULT_HOST "192.0.2.1"
#define DEFAULT_PORT 3000
#define QUEUE_PATH "/_nds/romsfinder.queue"
#define QUEUE_CAPACITY 32

typedef enum { SYSTEM_NDS, SYSTEM_GBA, SYSTEM_OTHERS, SYSTEM_COUNT } RomSystem;

typedef struct {
    char id[33], title[91], region[25], languages[41];
    uint32_t size;
    RomSystem system;
} Game;

typedef struct { Game items[QUEUE_CAPACITY]; unsigned count; } DownloadQueue;
typedef bool (*QueueTransfer)(const Game *game, void *context);
typedef bool (*QueueCheckpoint)(const DownloadQueue *queue, void *context);

typedef enum { WIFI_WPA2, WIFI_FIRMWARE } WifiMode;

typedef struct {
    char host[64];
    unsigned port;
    WifiMode wifiMode;
    char ssid[33];
    char password[65];
} Settings;

typedef struct {
    char state[16], name[140], message[160];
    uint32_t received, total, size, crc;
} Job;

typedef struct {
    int socket, status;
    uint32_t length, rangeStart, rangeTotal;
    unsigned pos, used, timeoutFrames;
    unsigned char buffer[8192];
} Http;

extern Settings settings;
extern char lastError[192];

bool appPoll(void);
bool appYield(void);
void progress(const char *title, const char *message, uint32_t done, uint32_t total);
bool httpOpen(Http *http, const char *method, const char *path, uint32_t offset);
bool httpOpenTimeout(Http *http, const char *method, const char *path, uint32_t offset, unsigned timeoutFrames);
int httpRead(Http *http, void *buffer, size_t length);
void httpClose(Http *http);
bool httpText(const char *method, const char *path, char *output, size_t capacity);
bool httpUpload(const char *path, FILE *file, uint32_t size);
void urlEncode(const char *source, char *dest, size_t capacity);

typedef struct { char id[33], path[224]; uint32_t size, crc; bool restore; } SaveCommand;
void savesConnect(void);
bool savesPoll(SaveCommand *command);
bool savesExecute(const SaveCommand *command);
void savesFinish(const SaveCommand *command, bool success, const char *message);

typedef struct { char id[33], path[224]; uint32_t size, modified, header; bool remove; } LibraryCommand;
void libraryConnect(const char *executable);
bool libraryPoll(LibraryCommand *command);
bool libraryExecute(const LibraryCommand *command);
void libraryFinish(const LibraryCommand *command, bool success);

bool parseCatalog(char *text, Game *games, int *count, int *total, int *page, int *pages);
bool parseJob(char *text, Job *job);
bool parseCatalogForSystem(char *text, Game *games, int *count, int *total, int *page, int *pages, RomSystem system);
bool parseJobForSystem(char *text, Job *job, RomSystem system);
const char *systemKey(RomSystem system);
const char *systemLabel(RomSystem system);
const char *downloadDirectory(RomSystem system);
const char *mediaDirectory(const char *name);
bool gameValid(const Game *game);
bool queueAdd(DownloadQueue *queue, const Game *game);
void queueRemove(DownloadQueue *queue, unsigned index);
bool queueWrite(FILE *file, const DownloadQueue *queue);
bool queueRead(FILE *file, DownloadQueue *queue);
bool queueRun(DownloadQueue *queue, QueueTransfer transfer, QueueCheckpoint checkpoint, void *context, unsigned *completed);
uint32_t crcUpdate(uint32_t crc, const void *data, size_t length);
uint64_t fileFingerprint(const char *path, uint32_t size, uint32_t modified);
bool installAppUpdate(const char *partial, const char *destination, char *error, size_t capacity);
bool installTheme(const char *package, char *error, size_t capacity);
bool installFolder(const char *package, char *error, size_t capacity);

bool settingsRead(FILE *file, Settings *options, char *error, size_t capacity);
bool settingsWrite(FILE *file, const Settings *options);
bool wifiSettingsValid(const Settings *options, char *error, size_t capacity);

bool wifiBegin(const Settings *options, char *error, size_t capacity);
