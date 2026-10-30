#include "app.h"
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

static unsigned char assetBuffer[4096];

static uint32_t little32(const unsigned char *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static bool relativePath(const char *name) {
    if (!*name || *name == '/' || strlen(name) > 220) return false;
    unsigned component = 0, depth = 0;
    for (const unsigned char *cursor = (const unsigned char *)name; ; cursor++) {
        if (*cursor && (*cursor < 32 || *cursor == 127 || strchr("\\:<>\"|?*", *cursor))) return false;
        if (*cursor == '/' || !*cursor) {
            if (!component || cursor[-1] == '.' || cursor[-1] == ' ' || ++depth > 16) return false;
            component = 0;
            if (!*cursor) break;
        } else component++;
    }
    return true;
}

static bool parents(const char *name) {
    char directory[512];
    if (strlen(name) >= sizeof(directory)) return false;
    strcpy(directory, name);
    for (char *cursor = directory + 1; *cursor; cursor++) {
        if (*cursor != '/') continue;
        *cursor = 0;
        if (mkdir(directory, 0777) && errno != EEXIST) return false;
        *cursor = '/';
    }
    return true;
}

static bool removeTree(const char *name, unsigned depth) {
    if (depth > 16) return false;
    DIR *directory = opendir(name);
    if (!directory) return errno == ENOENT;
    bool ok = true;
    struct dirent *entry;
    while (ok && (entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char child[512];
        struct stat info;
        if (snprintf(child, sizeof(child), "%s/%s", name, entry->d_name) >= (int)sizeof(child) || lstat(child, &info)) { ok = false; break; }
        ok = S_ISDIR(info.st_mode) ? removeTree(child, depth + 1) : !remove(child);
    }
    closedir(directory);
    return ok && !rmdir(name);
}

static bool themeRoot(const char *root) {
    static const char *roots[] = {
        "/_nds/TWiLightMenu/dsimenu/themes/", "/_nds/TWiLightMenu/3dsmenu/themes/",
        "/_nds/TWiLightMenu/r4menu/themes/", "/_nds/TWiLightMenu/akmenu/themes/"
    };
    for (unsigned index = 0; index < sizeof(roots) / sizeof(roots[0]); index++) {
        size_t length = strlen(roots[index]);
        if (!strncmp(root, roots[index], length) && relativePath(root + length) && !strchr(root + length, '/')) return true;
    }
    return false;
}

static bool folderRoot(const char *root) {
    static const char *roots[] = { "/roms/others/", "/videos/", "/music/", "/pictures/", "/multimedia/" };
    for (unsigned index = 0; index < sizeof(roots) / sizeof(roots[0]); index++) {
        size_t length = strlen(roots[index]);
        if (!strncmp(root, roots[index], length) && relativePath(root + length) && !strchr(root + length, '/')) return true;
    }
    return false;
}

static bool installPackage(const char *package, char *error, size_t capacity, bool folder) {
    char root[224], staging[256], backup[256], destination[512], relative[224];
    unsigned char header[12];
    FILE *input = fopen(package, "rb");
    bool ok = false, staged = false;
    snprintf(error, capacity, "Invalid file package");
    if (!input) return false;
    if (fread(header, 1, 12, input) != 12 || memcmp(header, folder ? "RFFILES1" : "RFTHEME1", 8)) goto finished;
    unsigned rootLength = header[8] | (header[9] << 8);
    unsigned count = header[10] | (header[11] << 8);
    if (!rootLength || rootLength >= sizeof(root) || !count || count > 2048) goto finished;
    if (fread(root, 1, rootLength, input) != rootLength) goto finished;
    root[rootLength] = 0;
    if (strlen(root) != rootLength || !(folder ? folderRoot(root) : themeRoot(root))) goto finished;
    snprintf(staging, sizeof(staging), "%s.dsidrop-part", root);
    snprintf(backup, sizeof(backup), "%s.dsidrop-bak", root);
    struct stat packageInfo;
    struct statvfs space;
    if (!stat(package, &packageInfo) && !statvfs("/", &space) &&
        (uint64_t)space.f_bavail * space.f_frsize < (uint64_t)packageInfo.st_size + 65536) {
        snprintf(error, capacity, "Not enough free space to install files"); goto finished;
    }
    if (!parents(root) || !removeTree(staging, 0) || mkdir(staging, 0777)) {
        snprintf(error, capacity, "Could not create staging folder"); goto finished;
    }
    staged = true;
    uint32_t total = 0;
    uint32_t fileLimit = folder ? MAX_GAME_SIZE : 32U * 1024U * 1024U;
    uint32_t totalLimit = folder ? MAX_GAME_SIZE : 128U * 1024U * 1024U;
    for (unsigned index = 0; index < count; index++) {
        if (fread(header, 1, 10, input) != 10) goto finished;
        unsigned length = header[0] | (header[1] << 8);
        uint32_t size = little32(header + 2), expected = little32(header + 6);
        if (!length || length >= sizeof(relative) || size > fileLimit || total > totalLimit - size) goto finished;
        total += size;
        if (fread(relative, 1, length, input) != length) goto finished;
        relative[length] = 0;
        if (strlen(relative) != length || !relativePath(relative)) goto finished;
        if (folder && rootLength + length + 1 > 220) goto finished;
        if (snprintf(destination, sizeof(destination), "%s/%s", staging, relative) >= (int)sizeof(destination) || !parents(destination)) goto finished;
        struct stat existing;
        if (!stat(destination, &existing)) goto finished;
        FILE *output = fopen(destination, "wb");
        if (!output) { snprintf(error, capacity, "Could not write package file"); goto finished; }
        uint32_t received = 0, crc = 0xffffffffU;
        bool written = true;
        progress(folder ? "Installing folder" : "Installing theme", relative, index, count);
        while (received < size) {
            size_t want = size - received;
            if (want > sizeof(assetBuffer)) want = sizeof(assetBuffer);
            size_t length = fread(assetBuffer, 1, want, input);
            if (length != want || fwrite(assetBuffer, 1, length, output) != length || !appYield()) { written = false; break; }
            crc = crcUpdate(crc, assetBuffer, length); received += length;
        }
        if (fclose(output)) written = false;
        if (!written || (crc ^ 0xffffffffU) != expected) { snprintf(error, capacity, "Package file failed verification"); goto finished; }
    }
    if (fgetc(input) != EOF || ferror(input)) goto finished;
    if (!removeTree(backup, 0)) { snprintf(error, capacity, "Could not clear previous folder backup"); goto finished; }
    struct stat existing;
    bool hadTheme = !stat(root, &existing);
    if (hadTheme && rename(root, backup)) { snprintf(error, capacity, "Could not back up existing folder"); goto finished; }
    if (rename(staging, root)) {
        bool restored = !hadTheme || !rename(backup, root);
        snprintf(error, capacity, restored ? "Install failed; previous folder preserved" : "Restore the .dsidrop-bak folder using your PC"); goto finished;
    }
    staged = false;
    ok = true;
    if (folder) snprintf(error, capacity, "Installed to %.140s/.", root);
    else snprintf(error, capacity, "Installed to %s/. Select this skin in TWiLight Menu++ Settings.", root);
finished:
    fclose(input);
    if (staged) removeTree(staging, 0);
    return ok;
}

bool installTheme(const char *package, char *error, size_t capacity) { return installPackage(package, error, capacity, false); }
bool installFolder(const char *package, char *error, size_t capacity) { return installPackage(package, error, capacity, true); }