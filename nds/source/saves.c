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

static char nativeReply[8192];

static void encodeSavePath(const char *path, char *encoded) {
    encoded[0] = 0;
    if (path) for (unsigned index = 0; index < strlen(path); index++)
        snprintf(encoded + index * 2, 3, "%02x", (unsigned char)path[index]);
}

static bool decodeSavePath(const char *encoded, char *path) {
    if (!encoded || !*encoded || strlen(encoded) > 440 || strlen(encoded) % 2 || strspn(encoded, "0123456789abcdef") != strlen(encoded)) return false;
    unsigned length = strlen(encoded) / 2;
    for (unsigned index = 0; index < length; index++) {
        char byte[3] = {encoded[index * 2], encoded[index * 2 + 1], 0};
        path[index] = strtoul(byte, NULL, 16);
        if (!path[index]) return false;
    }
    path[length] = 0;
    return validPath(path);
}

static bool nativeSaveRead(const char *method, const char *path, const char *view, char **state) {
    nativeReply[0] = 0;
    bool ok = httpText(method, path, nativeReply, sizeof(nativeReply));
    if (!ok && strncmp(nativeReply, "RFSAVES1\terror\t", 15)) return false;
    char *magic = strtok_r(nativeReply, "\t\r\n", state), *kind = strtok_r(NULL, "\t\r\n", state);
    if (ok && magic && !strcmp(magic, "RFSAVES1") && kind && !strcmp(kind, view)) return true;
    char *detail = kind && !strcmp(kind, "error") ? strtok_r(NULL, "\t\r\n", state) : NULL;
    snprintf(lastError, 192, "%.191s", detail ? detail : "Update the server to use save management");
    return false;
}

static bool nativeSaveGet(const char *view, const char *path, const char *backup, unsigned page, char **state) {
    if (!path || strlen(path) > 220) { strcpy(lastError, "Invalid SD path"); return false; }
    char encoded[448], url[640];
    encodeSavePath(path, encoded);
    snprintf(url, sizeof(url), "/api/dsi/saves?mac=%s&view=%s&page=%u&path=%s&backup=%s", clientMac(), view, page, encoded, backup ? backup : "");
    return nativeSaveRead("GET", url, view, state);
}

static bool nativeNumber(char **state, unsigned *number, unsigned limit) {
    char *value = strtok_r(NULL, "\t\r\n", state);
    if (!value || !*value || strlen(value) > 10 || strspn(value, "0123456789") != strlen(value)) return false;
    errno = 0;
    unsigned long parsed = strtoul(value, NULL, 10);
    if (errno || parsed > limit) return false;
    *number = parsed;
    return true;
}

bool savesGame(const char *path, Game *game) {
    char *state;
    if (!nativeSaveGet("game", path, NULL, 0, &state)) return false;
    char *system = strtok_r(NULL, "\t\r\n", &state), *id = strtok_r(NULL, "\t\r\n", &state), *title = strtok_r(NULL, "\t\r\n", &state);
    if (!system || !id || !title) return false;
    if (!strcmp(system, "-")) return true;
    RomSystem resolved = !strcmp(system, "nds") ? SYSTEM_NDS : !strcmp(system, "gba") ? SYSTEM_GBA : SYSTEM_OTHERS;
    unsigned length = resolved == SYSTEM_GBA ? 8 : 32;
    if (strlen(title) > 90 || (strcmp(id, "-") && (strlen(id) != length || strspn(id, "0123456789abcdef") != length))) return false;
    game->system = resolved;
    strcpy(game->id, !strcmp(id, "-") ? "" : id);
    strcpy(game->title, title);
    return true;
}

bool savesVersions(const char *path, unsigned page, SaveVersion *versions, unsigned *count, unsigned *total, bool *automatic) {
    *count = 0; *total = 0;
    char *state;
    if (!nativeSaveGet("backups", path, NULL, page, &state)) return false;
    unsigned amount, enabled;
    if (!nativeNumber(&state, &amount, PAGE_SIZE) || !nativeNumber(&state, total, 1000000) || !nativeNumber(&state, &enabled, 1) || *total < amount) goto invalid;
    *automatic = enabled != 0;
    for (unsigned index = 0; index < amount; index++) {
        SaveVersion *version = &versions[index];
        char *id = strtok_r(NULL, "\t\r\n", &state), *date = strtok_r(NULL, "\t\r\n", &state);
        unsigned size;
        if (!id || strlen(id) != 32 || strspn(id, "0123456789abcdef") != 32 || !date || strlen(date) != 16 ||
            !nativeNumber(&state, &size, 32U * 1024U * 1024U) || !size || !decodeSavePath(strtok_r(NULL, "\t\r\n", &state), version->path)) goto invalid;
        strcpy(version->id, id); strcpy(version->date, date); version->size = size;
    }
    if (strtok_r(NULL, "\t\r\n", &state)) goto invalid;
    *count = amount;
    return true;
invalid:
    strcpy(lastError, "Invalid backup list from server");
    return false;
}

bool savesTargets(const SaveVersion *version, unsigned page, SaveTarget *targets, unsigned *count, unsigned *total) {
    *count = 0; *total = 0;
    char *state;
    if (!nativeSaveGet("targets", version->path, version->id, page, &state)) return false;
    unsigned amount;
    if (!nativeNumber(&state, &amount, PAGE_SIZE) || !nativeNumber(&state, total, 1000000) || *total < amount) goto invalid;
    for (unsigned index = 0; index < amount; index++) {
        SaveTarget *target = &targets[index];
        char *mac = strtok_r(NULL, "\t\r\n", &state), *game = strtok_r(NULL, "\t\r\n", &state), *name = strtok_r(NULL, "\t\r\n", &state);
        if (!mac || strlen(mac) != 12 || strspn(mac, "0123456789abcdef") != 12 || !game || strlen(game) != 32 ||
            strspn(game, "0123456789abcdef") != 32 || !name || strlen(name) > 40 || !decodeSavePath(strtok_r(NULL, "\t\r\n", &state), target->path)) goto invalid;
        strcpy(target->mac, mac); strcpy(target->game, game); strcpy(target->name, name);
    }
    if (strtok_r(NULL, "\t\r\n", &state)) goto invalid;
    *count = amount;
    return true;
invalid:
    strcpy(lastError, "Invalid destination list from server");
    return false;
}

bool savesRequest(const char *action, const char *path, const char *backup, const SaveTarget *target, bool automatic) {
    if (path && strlen(path) > 220) { strcpy(lastError, "Invalid save path"); return false; }
    char source[448], destination[448], url[1400];
    encodeSavePath(path, source);
    snprintf(url, sizeof(url), "/api/dsi/saves?mac=%s&action=%s&enabled=%u%s%s&backup=%s", clientMac(), action,
        automatic ? 1 : 0, path ? "&path=" : "", source, backup ? backup : "");
    if (target) {
        encodeSavePath(target->path, destination);
        unsigned length = strlen(url);
        snprintf(url + length, sizeof(url) - length, "&targetMac=%s&targetGame=%s&targetPath=%s", target->mac, target->game, destination);
    }
    char *state;
    if (!nativeSaveRead("POST", url, "ok", &state)) return false;
    due = 0;
    return true;
}