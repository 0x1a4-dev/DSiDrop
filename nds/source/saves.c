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

static char session[24], finished[33], completion[160], fingerprint[17];
static bool completed, startupPending;
static uint64_t due, metadataFingerprint;
static unsigned savedCount;
static unsigned char saveBuffer[4096];
static char scanPath[224];
static bool scanSaves(const SaveCommand *command, unsigned depth);

static bool checksum(FILE *input, uint32_t *crc, uint32_t *size) {
    *crc = 0xffffffffU; *size = 0;
    size_t amount;
    while ((amount = fread(saveBuffer, 1, sizeof(saveBuffer), input))) {
        *crc = crcUpdate(*crc, saveBuffer, amount); *size += amount;
        if (!appPoll()) return false;
    }
    if (ferror(input)) { strcpy(lastError, "Could not verify save on SD"); return false; }
    *crc ^= 0xffffffffU;
    return true;
}

static bool validPath(const char *path) {
    size_t length = strlen(path);
    if (length < 6 || length > 220 || *path != '/' || strcasecmp(path + length - 4, ".sav")) return false;
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

void savesConnect(void) {
    if (*session) return;
    static unsigned connections;
    uint64_t ticks = tickGetCount();
    snprintf(session, sizeof(session), "%08lx%08lx", (unsigned long)(ticks >> 32), (unsigned long)ticks ^ ++connections);
    finished[0] = 0; due = 0; startupPending = true;
    metadataFingerprint = 0; strcpy(scanPath, "/");
    if (scanSaves(NULL, 0)) snprintf(fingerprint, sizeof(fingerprint), "%016llx", (unsigned long long)metadataFingerprint);
    lastError[0] = 0;
}

bool savesPoll(SaveCommand *command) {
    if (!clientRegistered() || !*session || tickGetCount() < due) return false;
    due = tickGetCount() + 15 * TICK_FREQ;
    char path[960], reply[640], encoded[480], metadata[32] = "";
    if (*fingerprint) snprintf(metadata, sizeof(metadata), "&fingerprint=%s", fingerprint);
    urlEncode(completion, encoded, sizeof(encoded));
    snprintf(path, sizeof(path), "/api/saves/sync?mac=%s&session=%s&cap=1&startup=%u&finished=%s&ok=%u&message=%s%s", clientMac(), session, startupPending ? 1 : 0, finished, completed ? 1 : 0, encoded, metadata);
    if (!httpText("POST", path, reply, sizeof(reply))) return false;
    finished[0] = 0; completion[0] = 0;
    char *state, *magic = strtok_r(reply, "\t\r\n", &state), *action = strtok_r(NULL, "\t\r\n", &state);
    if (!magic || strcmp(magic, "RFSAVE1") || !action) return false;
    startupPending = false;
    if (!strcmp(action, "none")) return false;
    char *id = strtok_r(NULL, "\t\r\n", &state);
    if (!id || strlen(id) != 32 || strspn(id, "0123456789abcdef") != 32 || (strcmp(action, "backup") && strcmp(action, "restore"))) return false;
    memset(command, 0, sizeof(*command)); strcpy(command->id, id);
    command->restore = !strcmp(action, "restore");
    if (!command->restore) {
        char *hex = strtok_r(NULL, "\t\r\n", &state);
        if (!hex) return true;
        if (!*hex || strlen(hex) > 440 || strlen(hex) % 2 || strspn(hex, "0123456789abcdef") != strlen(hex)) return false;
        for (unsigned index = 0; index < strlen(hex) / 2; index++) {
            char byte[3] = {hex[index * 2], hex[index * 2 + 1], 0};
            command->path[index] = strtoul(byte, NULL, 16);
            if (!command->path[index]) return false;
        }
        return validPath(command->path);
    }
    char *hex = strtok_r(NULL, "\t\r\n", &state), *size = strtok_r(NULL, "\t\r\n", &state), *crc = strtok_r(NULL, "\t\r\n", &state);
    if (!hex || !size || !crc || strlen(hex) > 440 || strlen(hex) % 2 || strspn(hex, "0123456789abcdef") != strlen(hex)) return false;
    for (unsigned index = 0; index < strlen(hex) / 2; index++) {
        char byte[3] = {hex[index * 2], hex[index * 2 + 1], 0};
        command->path[index] = strtoul(byte, NULL, 16);
        if (!command->path[index]) return false;
    }
    char *end; errno = 0;
    unsigned long length = strtoul(size, &end, 10);
    if (errno || *end || !length || length > 32U * 1024U * 1024U || strlen(crc) != 8 || strspn(crc, "0123456789abcdef") != 8 || !validPath(command->path)) return false;
    command->size = length; command->crc = strtoul(crc, NULL, 16);
    return true;
}

static bool uploadSave(const SaveCommand *command, const char *file) {
    struct stat info;
    if (!validPath(file) || stat(file, &info) || !S_ISREG(info.st_mode) || info.st_size < 1 || info.st_size > 32 * 1024 * 1024) { strcpy(lastError, "Invalid or oversized save file"); return false; }
    FILE *input = fopen(file, "rb");
    if (!input) { strcpy(lastError, "Could not open save file"); return false; }
    progress("Backing up save", file, 0, info.st_size);
    uint32_t crc = 0, checkedSize = 0;
    bool ok = checksum(input, &crc, &checkedSize);
    if (checkedSize != (uint32_t)info.st_size || fseek(input, 0, SEEK_SET)) { ok = false; if (!*lastError) strcpy(lastError, "Save file changed while reading"); }
    char hex[448], path[640];
    for (unsigned index = 0; index < strlen(file); index++) snprintf(hex + index * 2, 3, "%02x", (unsigned char)file[index]);
    snprintf(path, sizeof(path), "/api/saves/sync?mac=%s&job=%s&path=%s&crc=%08lx", clientMac(), command->id, hex, (unsigned long)crc);
    if (ok) ok = httpUpload(path, input, info.st_size);
    fclose(input);
    if (ok) savedCount++;
    return ok;
}

static bool scanSaves(const SaveCommand *command, unsigned depth) {
    if (depth > 12) { strcpy(lastError, "Save scan exceeds 12 folder levels"); return false; }
    DIR *directory = opendir(scanPath);
    if (!directory) { strcpy(lastError, "Could not read SD folder"); return false; }
    size_t base = strlen(scanPath);
    bool ok = true;
    struct dirent *entry;
    while (ok && (entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") || !strcmp(entry->d_name, "System Volume Information") || strstr(entry->d_name, ".dsidrop-bak") || strstr(entry->d_name, ".dsidrop-part")) continue;
        if (base + strlen(entry->d_name) + 2 >= sizeof(scanPath)) { strcpy(lastError, "SD path too long for save backup"); ok = false; break; }
        snprintf(scanPath + base, sizeof(scanPath) - base, "%s%s", base > 1 ? "/" : "", entry->d_name);
        struct stat info;
        if (lstat(scanPath, &info)) { strcpy(lastError, "Could not inspect SD entry"); ok = false; }
        else if (S_ISDIR(info.st_mode)) ok = scanSaves(command, depth + 1);
        else if (S_ISREG(info.st_mode) && info.st_size > 0 && strlen(scanPath) > 4 && !strcasecmp(scanPath + strlen(scanPath) - 4, ".sav")) {
            metadataFingerprint += fileFingerprint(scanPath, (uint32_t)info.st_size, (uint32_t)info.st_mtime);
            if (command) ok = uploadSave(command, scanPath);
        }
        scanPath[base] = 0;
        if (ok && !appPoll()) ok = false;
    }
    closedir(directory);
    return ok;
}

static bool restoreSave(const SaveCommand *command) {
    struct stat info;
    if (!stat(command->path, &info)) {
        if (!S_ISREG(info.st_mode)) { strcpy(lastError, "Restore destination is not a save file"); return false; }
        if (info.st_size > 0 && !uploadSave(command, command->path)) return false;
    } else if (errno != ENOENT) { strcpy(lastError, "Could not inspect current save"); return false; }
    char partial[256], backup[256], path[160], directory[224];
    snprintf(partial, sizeof(partial), "%s.dsidrop-part", command->path);
    snprintf(backup, sizeof(backup), "%s.dsidrop-bak", command->path);
    strcpy(directory, command->path);
    for (char *cursor = directory + 1; *cursor; cursor++) {
        if (*cursor != '/') continue;
        *cursor = 0;
        if (mkdir(directory, 0777) && errno != EEXIST) { strcpy(lastError, "Could not create save folder"); return false; }
        *cursor = '/';
    }
    struct statvfs space;
    if (!statvfs("/", &space) && (uint64_t)space.f_bavail * space.f_frsize < command->size + 65536U) { strcpy(lastError, "Not enough free SD space"); return false; }
    snprintf(path, sizeof(path), "/api/saves/file?mac=%s&job=%s", clientMac(), command->id);
    static Http http;
    if (!httpOpen(&http, "GET", path, 0)) return false;
    if (http.status != 200 || http.length != command->size) { httpClose(&http); strcpy(lastError, "Invalid save restore response"); return false; }
    FILE *output = fopen(partial, "wb");
    if (!output) { httpClose(&http); strcpy(lastError, "Could not write temporary save"); return false; }
    uint32_t received = 0, crc = 0xffffffffU;
    bool ok = true;
    while (received < command->size) {
        unsigned want = command->size - received;
        if (want > sizeof(saveBuffer)) want = sizeof(saveBuffer);
        int amount = httpRead(&http, saveBuffer, want);
        if (amount <= 0 || fwrite(saveBuffer, 1, amount, output) != (size_t)amount) { ok = false; break; }
        crc = crcUpdate(crc, saveBuffer, amount); received += amount;
        progress("Restoring save", command->path, received, command->size);
        if (!appYield()) { ok = false; break; }
    }
    if (fclose(output)) ok = false;
    httpClose(&http);
    if (!ok || (crc ^ 0xffffffffU) != command->crc) { remove(partial); if (!*lastError) strcpy(lastError, "Save checksum mismatch"); return false; }
    FILE *verification = fopen(partial, "rb");
    uint32_t verifiedSize = 0, verifiedCrc = 0;
    bool verified = verification && checksum(verification, &verifiedCrc, &verifiedSize);
    if (verification && fclose(verification)) verified = false;
    if (!verified || verifiedSize != command->size || verifiedCrc != command->crc) {
        remove(partial); if (!*lastError) strcpy(lastError, "Save verification on SD failed"); return false;
    }
    bool previous = !stat(command->path, &info);
    if (previous && remove(backup) && errno != ENOENT) { remove(partial); strcpy(lastError, "Could not clear previous save backup"); return false; }
    if (previous && rename(command->path, backup)) { remove(partial); strcpy(lastError, "Could not back up local save"); return false; }
    if (rename(partial, command->path)) {
        bool restored = !previous || !rename(backup, command->path);
        snprintf(lastError, 192, "%s", restored ? "Save restore failed; original preserved" : "Restore .sav.dsidrop-bak using your PC");
        return false;
    }
    return true;
}

bool savesExecute(const SaveCommand *command) {
    lastError[0] = 0; savedCount = 0;
    if (command->restore) return restoreSave(command);
    if (*command->path) return uploadSave(command, command->path);
    metadataFingerprint = 0; strcpy(scanPath, "/");
    bool ok = scanSaves(command, 0);
    if (ok) snprintf(fingerprint, sizeof(fingerprint), "%016llx", (unsigned long long)metadataFingerprint);
    return ok;
}

void savesFinish(const SaveCommand *command, bool success, const char *message) {
    strcpy(finished, command->id); completed = success;
    if (success) snprintf(completion, sizeof(completion), command->restore ? "Save restored; current save backed up" : "%u save files backed up", savedCount);
    else snprintf(completion, sizeof(completion), "%.159s", message);
    due = 0;
}