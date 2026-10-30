#include "app.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool failInstall;

static int renameWithFailure(const char *source, const char *destination) {
    if (failInstall && strstr(source, ".part")) { errno = EIO; return -1; }
    return rename(source, destination);
}

#define rename renameWithFailure
#include "../source/update.c"
#undef rename

static void writeFixture(const char *file, int value) {
    FILE *output = fopen(file, "wb");
    assert(output);
    for (unsigned index = 0; index < 1024; index++) assert(fputc(value, output) != EOF);
    assert(!fclose(output));
}

static void expectFixture(const char *file, int value) {
    FILE *input = fopen(file, "rb");
    assert(input);
    for (unsigned index = 0; index < 1024; index++) assert(fgetc(input) == value);
    assert(fgetc(input) == EOF);
    assert(!fclose(input));
}

int main(void) {
    char temporary[] = "build/tests/update-XXXXXX";
    assert(mkdtemp(temporary));
    char destination[200], partial[210], backup[210], error[192];
    snprintf(destination, sizeof(destination), "%s/DSiDrop.nds", temporary);
    snprintf(partial, sizeof(partial), "%s.part", destination);
    snprintf(backup, sizeof(backup), "%s.bak", destination);
    writeFixture(partial, 1);
    assert(installAppUpdate(partial, destination, error, sizeof(error)));
    expectFixture(destination, 1);
    assert(access(partial, F_OK));
    writeFixture(partial, 2);
    assert(installAppUpdate(partial, destination, error, sizeof(error)));
    expectFixture(destination, 2); expectFixture(backup, 1);
    writeFixture(partial, 3);
    failInstall = true;
    assert(!installAppUpdate(partial, destination, error, sizeof(error)));
    expectFixture(destination, 2); expectFixture(partial, 3);
    failInstall = false;
    assert(installAppUpdate(partial, destination, error, sizeof(error)));
    expectFixture(destination, 3); expectFixture(backup, 2);
    assert(!installAppUpdate(partial, destination, error, sizeof(error)));
    expectFixture(destination, 3); expectFixture(backup, 2);
    assert(!remove(backup)); assert(!mkdir(backup, 0777));
    char occupied[230]; snprintf(occupied, sizeof(occupied), "%s/keep", backup);
    writeFixture(occupied, 4); writeFixture(partial, 5);
    assert(!installAppUpdate(partial, destination, error, sizeof(error)));
    expectFixture(destination, 3); expectFixture(partial, 5);
    assert(!remove(occupied)); assert(!rmdir(backup));
    assert(!remove(destination)); assert(!remove(partial)); assert(!rmdir(temporary));
    puts("PASS: update install, backup, rollback and failed/missing file preservation");
    return 0;
}