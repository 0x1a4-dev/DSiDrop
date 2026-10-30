#include "app.h"
#include "client.h"
#include <nds.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#define INVENTORY_TEMP "/_nds/dsidrop-library.tmp"
static char session[24], finished[33], completion[160], executablePath[224], scanPath[224], fingerprint[17];
static bool completed, startupPending;
static uint64_t due, metadataFingerprint;
static unsigned entries;
static unsigned char inventoryBuffer[4096];
static bool scanDirectory(FILE *output, unsigned depth);

static bool safePath(const char *path) {
    size_t length = strlen(path);
    if (!length || length > 220 || *path != '/') return false;
    const char *part = path + 1;
    unsigned depth = 0;
    for (const char *cursor = part; ; cursor++) {
        if (*cursor && ((unsigned char)*cursor < 32 || *cursor == 127 || strchr("\\:<>\"|?*", *cursor))) return false;
        if (*cursor == '/' || !*cursor) {
            if (cursor == part || cursor[-1] == '.' || cursor[-1] == ' ' || ++depth > 13) return false;
            if (!*cursor) return true;
            part = cursor + 1;
        }
    }
}

static bool romPath(const char *path) {
    if (!safePath(path) || !strcasecmp(path, executablePath) || !strcasecmp(path, "/boot.nds")
        || !strncasecmp(path, "/_nds/", 6) || !strncasecmp(path, "/private/", 9) || !strncasecmp(path, "/3ds/", 5)) return false;
    const char *name = strrchr(path, '/') + 1, *extension = strrchr(name, '.');
    if (!extension || (strcasecmp(extension, ".nds") && strcasecmp(extension, ".gba"))) return false;
    if (!strcasecmp(name, "DSiDrop.nds") || (!strncasecmp(name, "DSiDrop [", 9) && strlen(name) == 22)) return false;
    return true;
}

static bool headerCrc(const char *path, uint32_t *crc) {
    FILE *input = fopen(path, "rb");
    if (!input) { strcpy(lastError, "Could not inspect ROM on SD"); return false; }
    size_t amount = fread(inventoryBuffer, 1, 512, input);
    bool ok = !ferror(input);
    if (fclose(input)) ok = false;
    if (ok) *crc = crcUpdate(0xffffffffU, inventoryBuffer, amount) ^ 0xffffffffU;
    else strcpy(lastError, "Could not read ROM header");
    return ok;
}

void libraryConnect(const char *executable) {
    if (*session) return;
    static unsigned connections;
    uint64_t ticks = tickGetCount();
    snprintf(session, sizeof(session), "%08lx%08lx", (unsigned long)(ticks >> 32), (unsigned long)ticks ^ ++connections);
    const char *colon = strchr(executable, ':');
    snprintf(executablePath, sizeof(executablePath), "%s", colon ? colon + 1 : executable);
    finished[0] = 0; completion[0] = 0; due = 0; startupPending = true;
    entries = 0; metadataFingerprint = 0; strcpy(scanPath, "/");
    if (scanDirectory(NULL, 0)) snprintf(fingerprint, sizeof(fingerprint), "%016llx", (unsigned long long)metadataFingerprint);
    lastError[0] = 0;
}

bool libraryPoll(LibraryCommand *command) {
    if (!clientRegistered() || !*session || tickGetCount() < due) return false;
    due = tickGetCount() + 15 * TICK_FREQ;
    char path[1024], reply[700], encoded[480], storage[96] = "", metadata[32] = "";
    if (*fingerprint) snprintf(metadata, sizeof(metadata), "&fingerprint=%s", fingerprint);
    struct statvfs space;
    if (!statvfs("/", &space)) {
        uint64_t blockSize = space.f_frsize ? space.f_frsize : space.f_bsize;
        uint64_t total = (uint64_t)space.f_blocks * blockSize, available = (uint64_t)space.f_bavail * blockSize;
        if (total && available <= total) snprintf(storage, sizeof(storage), "&total=%llu&free=%llu", (unsigned long long)total, (unsigned long long)available);
    }
    urlEncode(completion, encoded, sizeof(encoded));
    snprintf(path, sizeof(path), "/api/devices/library/sync?mac=%s&session=%s&cap=1&startup=%u&finished=%s&ok=%u&message=%s%s%s", clientMac(), session, startupPending ? 1 : 0, finished, completed ? 1 : 0, encoded, storage, metadata);
    if (!httpText("POST", path, reply, sizeof(reply))) return false;
    finished[0] = 0; completion[0] = 0;
    char *state, *magic = strtok_r(reply, "\t\r\n", &state), *action = strtok_r(NULL, "\t\r\n", &state);
    if (!magic || strcmp(magic, "RFLIBRARY1") || !action) return false;
    startupPending = false;
    if (!strcmp(action, "none")) return false;
    char *id = strtok_r(NULL, "\t\r\n", &state);
    if (!id || strlen(id) != 32 || strspn(id, "0123456789abcdef") != 32 || (strcmp(action, "scan") && strcmp(action, "delete"))) return false;
    memset(command, 0, sizeof(*command)); strcpy(command->id, id);
    command->remove = !strcmp(action, "delete");
    if (!command->remove) return true;
    char *hex = strtok_r(NULL, "\t\r\n", &state), *size = strtok_r(NULL, "\t\r\n", &state);
    char *modified = strtok_r(NULL, "\t\r\n", &state), *header = strtok_r(NULL, "\t\r\n", &state);
    if (!hex || !size || !modified || !header || strlen(hex) > 440 || !*hex || strlen(hex) % 2 || strspn(hex, "0123456789abcdef") != strlen(hex)
        || !*size || strspn(size, "0123456789") != strlen(size) || !*modified || strspn(modified, "0123456789") != strlen(modified)
        || strlen(header) != 8 || strspn(header, "0123456789abcdef") != 8) return false;
    for (unsigned index = 0; index < strlen(hex) / 2; index++) {
        char byte[3] = {hex[index * 2], hex[index * 2 + 1], 0};
        command->path[index] = strtoul(byte, NULL, 16);
        if (!command->path[index]) return false;
    }
    errno = 0;
    command->size = strtoul(size, NULL, 10); command->modified = strtoul(modified, NULL, 10); command->header = strtoul(header, NULL, 16);
    if (errno || command->size > MAX_GAME_SIZE || !romPath(command->path)) {
        strcpy(finished, command->id); completed = false; strcpy(completion, "Protected or invalid ROM path"); due = 0;
        return false;
    }
    return true;
}

static bool inventoryEntry(FILE *output, const struct stat *info) {
    if (++entries > 4096) { strcpy(lastError, "SD inventory exceeds 4096 files"); return false; }
    metadataFingerprint += fileFingerprint(scanPath, (uint32_t)info->st_size, (uint32_t)info->st_mtime);
    if (!output) return true;
    char encoded[448];
    for (unsigned index = 0; index < strlen(scanPath); index++) snprintf(encoded + index * 2, 3, "%02x", (unsigned char)scanPath[index]);
    uint32_t header = 0;
    if (romPath(scanPath) && !headerCrc(scanPath, &header)) return false;
    if (fprintf(output, "%s\t%lu\t%lu\t%08lx\n", encoded, (unsigned long)info->st_size, (unsigned long)(uint32_t)info->st_mtime, (unsigned long)header) < 0) {
        strcpy(lastError, "Could not write SD inventory"); return false;
    }
    progress("Reading games on SD", scanPath, entries, 0);
    return true;
}

static bool scanDirectory(FILE *output, unsigned depth) {
    if (depth > 12) { strcpy(lastError, "SD inventory exceeds 12 folder levels"); return false; }
    DIR *directory = opendir(scanPath);
    if (!directory) { strcpy(lastError, "Could not read SD directory"); return false; }
    size_t base = strlen(scanPath);
    struct dirent *entry;
    bool ok = true;
    while (ok && (entry = readdir(directory))) {
        const char *name = entry->d_name;
        if (*name == '.' || !strcasecmp(name, "_nds") || !strcasecmp(name, "private") || !strcasecmp(name, "3ds") || !strcasecmp(name, "System Volume Information")) continue;
        if (base + strlen(name) + 2 >= sizeof(scanPath)) { strcpy(lastError, "SD inventory path is too long"); ok = false; break; }
        snprintf(scanPath + base, sizeof(scanPath) - base, "%s%s", base > 1 ? "/" : "", name);
        struct stat info;
        if (lstat(scanPath, &info)) { strcpy(lastError, "Could not inspect SD entry"); ok = false; }
        else if (S_ISDIR(info.st_mode)) ok = scanDirectory(output, depth + 1);
        else if (S_ISREG(info.st_mode) && info.st_size >= 0 && (uint64_t)info.st_size <= MAX_GAME_SIZE && safePath(scanPath)) {
            const char *extension = strrchr(scanPath, '.');
            if (romPath(scanPath) || (extension && !strcasecmp(extension, ".sav"))) ok = inventoryEntry(output, &info);
        }
        scanPath[base] = 0;
        if (ok && !appPoll()) ok = false;
    }
    closedir(directory);
    return ok;
}

static bool uploadInventory(const LibraryCommand *command) {
    if (mkdir("/_nds", 0777) && errno != EEXIST) { strcpy(lastError, "Could not create inventory directory"); return false; }
    FILE *output = fopen(INVENTORY_TEMP, "w+b");
    if (!output) { strcpy(lastError, "Could not create SD inventory"); return false; }
    entries = 0; metadataFingerprint = 0; strcpy(scanPath, "/");
    bool ok = fputs("RFINVENTORY1\n", output) >= 0 && scanDirectory(output, 0);
    if (fflush(output) || fseek(output, 0, SEEK_SET)) ok = false;
    uint32_t size = 0, crc = 0xffffffffU;
    size_t amount;
    while (ok && (amount = fread(inventoryBuffer, 1, sizeof(inventoryBuffer), output))) {
        size += amount; crc = crcUpdate(crc, inventoryBuffer, amount);
        if (size > 2U * 1024U * 1024U) { strcpy(lastError, "SD inventory is too large"); ok = false; }
        if (ok && !appPoll()) ok = false;
    }
    if (ferror(output) || fseek(output, 0, SEEK_SET)) ok = false;
    char path[224];
    snprintf(path, sizeof(path), "/api/devices/library/sync?mac=%s&job=%s&crc=%08lx&fingerprint=%016llx", clientMac(), command->id, (unsigned long)(crc ^ 0xffffffffU), (unsigned long long)metadataFingerprint);
    if (ok) ok = httpUpload(path, output, size);
    if (ok) snprintf(fingerprint, sizeof(fingerprint), "%016llx", (unsigned long long)metadataFingerprint);
    if (fclose(output)) ok = false;
    remove(INVENTORY_TEMP);
    if (!ok && !*lastError) strcpy(lastError, "Could not publish SD inventory");
    return ok;
}

bool libraryExecute(const LibraryCommand *command) {
    lastError[0] = 0;
    if (!command->remove) return uploadInventory(command);
    if (!romPath(command->path)) { strcpy(lastError, "Protected ROM path"); return false; }
    struct stat info;
    if (lstat(command->path, &info)) {
        if (errno == ENOENT) return true;
        strcpy(lastError, "Could not inspect ROM"); return false;
    }
    uint32_t header;
    if (!S_ISREG(info.st_mode) || (uint64_t)info.st_size != command->size || (uint32_t)info.st_mtime != command->modified || !headerCrc(command->path, &header) || header != command->header) {
        strcpy(lastError, "ROM changed; refresh the games list first"); return false;
    }
    if (remove(command->path)) { strcpy(lastError, "Could not delete ROM from SD"); return false; }
    return true;
}

void libraryFinish(const LibraryCommand *command, bool success) {
    strcpy(finished, command->id); completed = success;
    snprintf(completion, sizeof(completion), "%.159s", success ? command->remove ? "ROM deleted; saves preserved" : "Games list updated" : lastError);
    due = 0;
}