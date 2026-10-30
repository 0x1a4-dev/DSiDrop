#include "app.h"
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

bool installAppUpdate(const char *partial, const char *destination, char *error, size_t capacity) {
    char backup[210];
    int length = snprintf(backup, sizeof(backup), "%s.bak", destination);
    if (length < 0 || (size_t)length >= sizeof(backup) || !strcmp(partial, destination)) {
        snprintf(error, capacity, "Invalid update destination."); return false;
    }
    struct stat info;
    if (stat(partial, &info) || !S_ISREG(info.st_mode) || info.st_size < 512) {
        snprintf(error, capacity, "The verified update file is missing."); return false;
    }
    bool existing = stat(destination, &info) == 0;
    if ((!existing && errno != ENOENT) || (existing && !S_ISREG(info.st_mode))) {
        snprintf(error, capacity, "Could not access the installed app."); return false;
    }
    if (existing) {
        if (remove(backup) && errno != ENOENT) {
            snprintf(error, capacity, "Could not replace the previous backup. The installed app is unchanged."); return false;
        }
        if (rename(destination, backup)) {
            snprintf(error, capacity, "Could not back up the installed app. It is unchanged."); return false;
        }
    }
    if (rename(partial, destination)) {
        if (existing && rename(backup, destination))
            snprintf(error, capacity, "Install failed. Restore the .nds.bak file using your PC.");
        else snprintf(error, capacity, "Install failed. The previous app is unchanged; retry the update.");
        return false;
    }
    return true;
}