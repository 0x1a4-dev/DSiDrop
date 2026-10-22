#include "client.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static char *column(char **cursor, char separator) {
    if (!*cursor) return NULL;
    char *result = *cursor, *end = strchr(result, separator);
    if (end) { *end = 0; *cursor = end + 1; } else *cursor = NULL;
    return result;
}

static bool decimal(const char *value, uint32_t *result) {
    if (!value || !*value || strspn(value, "0123456789") != strlen(value)) return false;
    char *end; errno = 0;
    unsigned long n = strtoul(value, &end, 10);
    if (errno || *end || n > UINT32_MAX) return false;
    *result = n; return true;
}

static bool copy(char *dest, size_t size, const char *value) {
    if (!value || strlen(value) >= size) return false;
    memcpy(dest, value, strlen(value) + 1); return true;
}

static bool taskId(const char *value) {
    return value && strlen(value) == 32 && strspn(value, "0123456789abcdef") == 32;
}

bool clientParse(char *text, ClientReply *reply) {
    memset(reply, 0, sizeof(*reply));
    char *newline = strchr(text, '\n');
    if (!newline || newline[1]) return false;
    *newline = 0;
    char *fields = text;
    const char *magic = column(&fields, '\t');
    uint32_t interval;
    if (!magic || strcmp(magic, "RFCLIENT1") || !decimal(column(&fields, '\t'), &interval) || interval < 1000 || interval > 60000) return false;
    const char *id = column(&fields, '\t'), *system = column(&fields, '\t');
    const char *rom = column(&fields, '\t'), *title = column(&fields, '\t');
    uint32_t size;
    if (!id || !system || !rom || !title || !decimal(column(&fields, '\t'), &size) || fields) return false;
    reply->interval = interval;
    if (!strcmp(id, "-")) return !strcmp(system, "-") && !strcmp(rom, "-") && !strcmp(title, "-") && !size;
    if (!taskId(id)) return false;
    Game *game = &reply->task.game;
    if (!strcmp(system, "nds")) game->system = SYSTEM_NDS;
    else if (!strcmp(system, "gba")) game->system = SYSTEM_GBA;
    else if (!strcmp(system, "others")) game->system = SYSTEM_OTHERS;
    else return false;
    game->size = size;
    strcpy(game->region, "From your PC"); strcpy(game->languages, "Unknown");
    return copy(reply->task.task, sizeof(reply->task.task), id) && copy(game->id, sizeof(game->id), rom) &&
        copy(game->title, sizeof(game->title), title) && gameValid(game);
}

bool clientTaskWrite(FILE *file, const ClientTask *task, const Settings *options, const char *mac) {
    if (!taskId(task->task) || !gameValid(&task->game) || (unsigned)task->status > CLIENT_CANCELED) return false;
    char record[512];
    if (strlen(mac) != 12 || strspn(mac, "0123456789abcdef") != 12) return false;
    int n = snprintf(record, sizeof(record), "%s\t%s\t%u\t%u\t%s\t%s\t%s\t%s\t%lu\n", mac, options->host, options->port,
        (unsigned)task->status, task->task, systemKey(task->game.system), task->game.id, task->game.title, (unsigned long)task->game.size);
    if (n < 0 || n >= (int)sizeof(record)) return false;
    uint32_t crc = crcUpdate(0xffffffffU, record, n) ^ 0xffffffffU;
    return fprintf(file, "RFCLIENTSTATE2\t%08lx\n%s", (unsigned long)crc, record) > 0 && !ferror(file);
}

bool clientTaskRead(FILE *file, ClientTask *task, const Settings *options, const char *mac) {
    char header[40], record[512]; unsigned long crc;
    if (!fgets(header, sizeof(header), file) || sscanf(header, "RFCLIENTSTATE2\t%8lx", &crc) != 1 ||
        !fgets(record, sizeof(record), file) || !strchr(record, '\n') || fgetc(file) != EOF) return false;
    if ((crcUpdate(0xffffffffU, record, strlen(record)) ^ 0xffffffffU) != crc) return false;
    char *fields = record;
    const char *owner = column(&fields, '\t');
    if (!owner || strcmp(owner, mac)) return false;
    const char *host = column(&fields, '\t'); uint32_t port, status;
    if (!host || strcmp(host, options->host) || !decimal(column(&fields, '\t'), &port) || port != options->port ||
        !decimal(column(&fields, '\t'), &status) || status > CLIENT_CANCELED) return false;
    char protocol[512];
    int n = snprintf(protocol, sizeof(protocol), "RFCLIENT1\t3000\t%s", fields ? fields : "");
    ClientReply reply;
    if (n < 0 || n >= (int)sizeof(protocol) || !clientParse(protocol, &reply) || !*reply.task.task) return false;
    *task = reply.task; task->status = status; return true;
}
