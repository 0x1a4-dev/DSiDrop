#include "app.h"
#include "ui.h"
#include "cover.h"
#include "client.h"
#include <nds.h>
#include <dswifi9.h>
#include <wfc.h>
#include <fat.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#if defined(ROMSFINDER_DISPLAY_TEST) || defined(ROMSFINDER_SYSTEM_TEST)
#include <assert.h>
#endif
#ifdef ROMSFINDER_PROTOCOL_TEST
#define main runProtocolTests
#include "../tests/protocol_test.c"
#undef main
#endif
#ifdef ROMSFINDER_QUEUE_TEST
static FILE *queueTestMemoryFile(void) {
    static char bytes[2048];
    memset(bytes, 0, sizeof(bytes));
    return fmemopen(bytes, sizeof(bytes), "w+");
}
#define tmpfile queueTestMemoryFile
#define main runQueueTests
#include "../tests/queue_test.c"
#undef main
#undef tmpfile
#endif
#ifdef ROMSFINDER_CLIENT_TEST
#include "../tests/client_test.c"
int runClientNetworkTests(void);
#endif
#ifdef ROMSFINDER_COVER_TEST
int runCoverTests(void);
#endif

Settings settings = { .host = DEFAULT_HOST, .port = DEFAULT_PORT, .wifiMode = WIFI_WPA2 };
char lastError[192];
static PrintConsole top, bottom;
static Game games[PAGE_SIZE];
static char query[81], response[4096];
static char appPath[200] = "/DSiDrop.nds";
static bool appPathKnown;
// Calico's default main stack has less than 16 KiB available in DTCM.
// File verification and transfer run sequentially; share a main-RAM buffer.
static unsigned char fileBuffer[16384];
static Http downloadHttp;
static int count, total, page, pages = 1, selected, regionIndex;
static bool quitting, wifiInitialized;
static RomSystem activeSystem = SYSTEM_NDS;
static DownloadQueue downloads;
static bool queueRunning;
static unsigned queueCompleted, queueBatchSize;
static RomSystem transferSystem;
static uint64_t sdSpaceDue, availableSdBytes;
static bool sdSpaceKnown;
static char browserNotice[48];
static bool browserVisible;
static int deferredKeys;
static int navigationRequest;
static Game sdDetail;
static touchPosition lastTouch;
static bool startupVisible, settingsVisible;
static void drawSettings(void);
static int navigationAt(int x);
static int selectNavigation(unsigned active);
static unsigned settingsFocus;
static unsigned startupStage, startupTick;
static char startupDetail[96];
#define ACTION_ADD_QUEUE (1 << 20)
#define ACTION_REGION (1 << 21)
#define ACTION_NDS (1 << 22)
#define ACTION_GBA (1 << 23)
#define ACTION_OTHERS (1 << 24)
#define ACTION_RANDOM (1 << 25)
#define ACTION_SD (1 << 26)
#define ACTION_SELECT (1 << 27)
#define ACTION_SETUP (1 << 28)
#define ACTION_SYSTEMS (ACTION_NDS | ACTION_GBA | ACTION_OTHERS)
static const char *regions[] = { "", "Europe", "Germany", "USA", "Japan", "World" };
static const char *regionLabels[] = { "All regions", "Europe", "Germany", "USA", "Japan", "World" };
static const char *othersTypes[] = { "", "Apps", "Uploaded", "Themes", "Multimedia" };
static const char *othersLabels[] = { "All", "Apps", "Your uploads", "Themes", "Multimedia" };
static const char *themeStyles[] = { "", "dsimenu", "3dsmenu", "r4menu", "akmenu" };
static const char *themeStyleLabels[] = { "All menu styles", "Nintendo DSi", "Nintendo 3DS", "R4 Original", "Wood UI" };
static unsigned themeStyle;
static bool themeFavoritesOnly;

static unsigned filterCount(void) {
    return activeSystem == SYSTEM_OTHERS ? sizeof(othersTypes) / sizeof(othersTypes[0]) : sizeof(regions) / sizeof(regions[0]);
}

static const char *filterLabel(unsigned index) {
    return activeSystem == SYSTEM_OTHERS ? othersLabels[index] : regionLabels[index];
}

static int catalogInput(int pending) {
    return keysDown() | (keysDownRepeat() & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) | pending;
}

static RomSystem requestedSystem(int key) {
    if (key & ACTION_NDS) return SYSTEM_NDS;
    if (key & ACTION_GBA) return SYSTEM_GBA;
    if (key & ACTION_OTHERS) return SYSTEM_OTHERS;
    if (key & KEY_L) return (activeSystem + SYSTEM_COUNT - 1) % SYSTEM_COUNT;
    return (activeSystem + 1) % SYSTEM_COUNT;
}

static RomSystem visibleSystem(unsigned slot) {
    return (activeSystem == SYSTEM_NDS ? SYSTEM_NDS : SYSTEM_GBA) + slot;
}

static int systemTouchAction(int x) {
    static const int actions[] = { ACTION_NDS, ACTION_GBA, ACTION_OTHERS };
    return actions[visibleSystem(x < 72 ? 0 : 1)];
}

static void clearKeyboardBackground(Keyboard *keyboard) {
    // The 256x512 keyboard BG wraps above the visible keys when shown. libnds
    // only uploads the ten key rows; the remaining rows contain loader VRAM.
    // A dedicated transparent tile makes that unused area reveal the console.
    const unsigned blankTile = 1023;
    u16 *map = bgGetMapPtr(keyboard->background);
    u8 *tiles = (u8 *)bgGetGfxPtr(keyboard->background);
    dmaFillHalfWords(0, tiles + blankTile * 32, 32);
    dmaFillHalfWords(blankTile, map, 32 * 64 * sizeof(u16));
    const KeyMap *layout = keyboard->mappings[keyboard->state];
    unsigned mapBytes = layout->width * layout->height * keyboard->grid_width * keyboard->grid_height / 64 * sizeof(u16);
    dmaCopy(layout->mapDataReleased, map, mapBytes);
}

static void clear(PrintConsole *screen) {
    if (screen == &bottom) { browserVisible = false; startupVisible = false; settingsVisible = false; }
    consoleSelect(screen); consoleClear();
    uiClear(screen == &top);
    iprintf("\x1b[39m");
}

static void frame(PrintConsole *screen, const char *title) {
    clear(screen);
    uiHeading(screen == &top, title, 16, 0, 232);
}

static void symbolButton(int x, int y, int width, int height, unsigned background, UiSymbol symbol, const char *label) {
    uiButton(x, y, width, height, background);
    int top = y + (height - 16) / 2;
    uiSymbol(false, x + 8, top, symbol, background == 4 ? 7 : 5);
    if (width < 128) uiCaption(false, label, x + 30, top, width - 38, 7);
    else uiText(false, label, x + 36, top, width - 44, 1);
}

static void drawNavigation(unsigned active) {
    static const char *labels[] = {"Catalog", "SD card", "Transfers", "Settings"};
    for (unsigned slot = 0; slot < 4; slot++) {
        uiButton(slot * 64, 164, 64, 28, slot == active ? 8 : 2);
        uiSymbol(false, slot * 64 + 24, 165, (UiSymbol)slot, slot == active ? 5 : 6);
        uiCenteredCaption(false, labels[slot], slot * 64, 179, 64, slot == active ? 5 : 6);
    }
}

static void drawTopHeading(const char *title) {
    clear(&top);
    if (!strcmp(title, APP_NAME)) uiFeatureBackdrop();
    uiHeading(true, title, 16, 0, 196);
    uiIcon(true, 216, 0);
}

static void formatStorageSize(uint64_t bytes, char *output, size_t capacity) {
    uint64_t divisor = 1;
    const char *unit = "B";
    if (bytes >= 1024ULL * 1024 * 1024) { divisor = 1024ULL * 1024 * 1024; unit = "GiB"; }
    else if (bytes >= 1024 * 1024) { divisor = 1024 * 1024; unit = "MiB"; }
    else if (bytes >= 1024) { divisor = 1024; unit = "KiB"; }
    unsigned fraction = (bytes % divisor) * 10 / divisor;
    if (fraction) snprintf(output, capacity, "%llu.%c %s", (unsigned long long)(bytes / divisor), '0' + (int)fraction, unit);
    else snprintf(output, capacity, "%llu %s", (unsigned long long)(bytes / divisor), unit);
}

static void readSdSpace(bool force) {
    uint64_t now = tickGetCount();
    if (!force && now < sdSpaceDue) return;
    sdSpaceDue = now + 5 * TICK_FREQ;
    struct statvfs space;
    sdSpaceKnown = statvfs("/", &space) == 0;
    if (sdSpaceKnown) {
        uint64_t blockSize = space.f_frsize ? space.f_frsize : space.f_bsize;
        sdSpaceKnown = blockSize && space.f_bavail <= space.f_blocks;
        if (sdSpaceKnown) availableSdBytes = (uint64_t)space.f_bavail * blockSize;
    }
}

static void drawFocusedGame(const Game *game, const char *notice) {
    drawTopHeading(APP_NAME);
    bool known = true;
    const u16 *pixels = *game->id ? coverFind(game, &known) : NULL;
    if (pixels) uiTopCover(pixels);
    else {
        uiSymbol(true, 120, 64, UI_CATALOG, 4);
        uiCenteredCaption(true, known ? "No artwork" : "Loading artwork", 72, 94, 112, 6);
    }
    uiCenteredText(true, game->title, 16, 140, 224, 2);
    char metadata[96], gameSize[24], freeSpace[24];
    snprintf(metadata, sizeof(metadata), "%s / %.24s", systemLabel(game->system), game->region);
    uiCaption(true, notice && *notice ? notice : metadata, 100, 5, 108, 6);
    readSdSpace(false);
    if (game->size) formatStorageSize(game->size, gameSize, sizeof(gameSize)); else strcpy(gameSize, "Unknown size");
    if (sdSpaceKnown) {
        formatStorageSize(availableSdBytes, freeSpace, sizeof(freeSpace));
        snprintf(metadata, sizeof(metadata), "Game %s / SD %s free", gameSize, freeSpace);
    } else snprintf(metadata, sizeof(metadata), "Game %s / SD unavailable", gameSize);
    uiPanel(true, 8, 172, 240, 20, 10);
    uiCenteredCaption(true, metadata, 12, 176, 232, 7);
}

static void drawBrand(void) {
    clear(&top); uiBrand(true);
    uiCenteredText(true, "Your library. On your DSi.", 16, 138, 224, 1);
    uiCenteredCaption(true, "NDS  /  GBA  /  Others", 16, 164, 224, 6);
    uiCenteredCaption(true, APP_VERSION, 16, 180, 224, 14);
}

static bool __attribute__((unused)) playIntro(void) {
    clear(&top); clear(&bottom);
#ifdef ROMSFINDER_INTRO_TEST
    u32 introDisplay = REG_DISPCNT;
    u8 introBank = VRAM_B_CR;
    u16 introBlend[] = {REG_BLDCNT, REG_BLDCNT_SUB, REG_BLDALPHA, REG_BLDALPHA_SUB};
#endif
    bool animated = uiIntroBegin();
    uiPresent();
    scanKeys();
    unsigned heldAtLaunch = keysHeld() & (KEY_A | KEY_B | KEY_START | KEY_TOUCH);
    if (animated) for (unsigned frame = 0; frame < UI_INTRO_FRAMES; frame++) {
        if (!pmMainLoop()) { uiIntroEnd(); return false; }
        swiWaitForVBlank(); scanKeys();
        clientTick(false);
        heldAtLaunch &= keysHeld();
        if (keysDown() & (KEY_A | KEY_B | KEY_START | KEY_TOUCH) & ~heldAtLaunch) break;
        uiIntroFrame(frame);
    }
    uiIntroEnd();
#ifdef ROMSFINDER_INTRO_TEST
    sassert(REG_DISPCNT == introDisplay && VRAM_B_CR == introBank, "Intro did not restore main display/VRAM");
    sassert(REG_BLDCNT == introBlend[0] && REG_BLDCNT_SUB == introBlend[1] &&
        REG_BLDALPHA == introBlend[2] && REG_BLDALPHA_SUB == introBlend[3], "Intro did not restore blending");
    sassert(!(REG_MASTER_BRIGHT & 31) && !(REG_MASTER_BRIGHT_SUB & 31), "Intro left display dimmed");
    for (unsigned id = 8; id < 21; id++)
        sassert(oamSub.oamMemory[id].attribute[0] == ATTR0_DISABLED, "Intro left a lower-screen sprite visible");
#endif
    drawBrand(); uiPresent();
    return true;
}

static void drawStartup(void) {
    clear(&bottom);
    uiHeading(false, "Getting ready", 16, 0, 224);
    uiCaption(false, "Connecting your library", 16, 28, 224, 6);
    static const char *labels[] = {"SD card", "Wi-Fi network", "PC library"};
    for (unsigned i = 0; i < 3; i++) {
        unsigned state = i < startupStage ? 2 : i == startupStage ? 1 : 0;
        int y = 44 + i * 38;
        uiPanel(false, 8, y, 240, 34, state == 1 ? 8 : 2);
        uiStageMark(16, y + 8, state);
        uiText(false, labels[i], 42, y, 196, 1);
        uiCaption(false, state == 2 ? "Ready" : state == 1 ? startupDetail : "Waiting", 42, y + 18, 196, 6);
    }
    uiBusy(startupTick * 3);
    symbolButton(8, 168, 240, 24, 2, UI_CLOSE, "Cancel");
    startupVisible = true;
    uiPresent();
}

static void startup(unsigned stage, const char *detail) {
    startupStage = stage;
    snprintf(startupDetail, sizeof(startupDetail), "%s", detail);
    drawStartup();
}

static int waitKey(void) {
    uiPresent();
    while (pmMainLoop()) {
        if (startupVisible && ++startupTick % 10 == 0) drawStartup();
        swiWaitForVBlank(); scanKeys(); uiAnimate();
        unsigned revision = clientRevision();
        clientTick(false);
        if (settingsVisible && revision != clientRevision()) drawSettings();
        int key = catalogInput(0);
        if (key & KEY_TOUCH) {
            touchRead(&lastTouch);
            if (!uiTouchFeedback(lastTouch.px, lastTouch.py)) key &= ~KEY_TOUCH;
        }
        if (key) return key;
    }
    quitting = true;
    return KEY_B;
}

static bool pollApp(bool wait) {
    static uint64_t lastPoll;
    if (!wait && tickGetCount() - lastPoll < ticksFromUsec(16667)) return !quitting;
    if (!pmMainLoop()) { quitting = true; strcpy(lastError, "Exiting"); return false; }
    if (startupVisible && ++startupTick % 10 == 0) drawStartup();
    uiPresent();
    if (wait) swiWaitForVBlank();
    scanKeys();
    lastPoll = tickGetCount();
    clientTick(false);
    bool cancel = (keysDown() & KEY_B) != 0;
    if (keysDown() & KEY_TOUCH) {
        touchPosition touch; touchRead(&touch);
        if (touch.py >= 168 && uiTouchFeedback(touch.px, touch.py)) cancel = true;
    }
    if (cancel) { strcpy(lastError, "Paused; partial file kept on SD"); return false; }
    return true;
}

bool appPoll(void) { return pollApp(false); }
bool appYield(void) { return pollApp(true); }

void progress(const char *title, const char *message, uint32_t done, uint32_t size) {
    if (clientActive()) clientProgress(done, size, message);
    char heading[40];
    if (queueRunning) snprintf(heading, sizeof(heading), "Downloads %u of %u", queueCompleted + 1, queueBatchSize);
    else snprintf(heading, sizeof(heading), "%s", clientActive() ? "From your library" : size ? "Transfer" : "Connecting");
    frame(&bottom, heading);
    uiText(false, title, 16, 34, 224, 3);
    uiText(false, message, 16, 84, 224, 1);
    if (size) {
        unsigned percent = (unsigned)((uint64_t)done * 100 / size);
        if (percent > 100) percent = 100;
        uiMeter(percent);
        char detail[64]; snprintf(detail, sizeof(detail), "%lu / %lu KiB", (unsigned long)(done / 1024), (unsigned long)(size / 1024));
        uiCenteredCaption(false, detail, 16, 152, 224, 6);
    } else {
        static unsigned phase;
        uiBusy(++phase * 3);
        uiCenteredCaption(false, "Working", 16, 126, 224, 6);
    }
    symbolButton(8, 168, 240, 24, 2, UI_PAUSE, "Pause");
    uiPresent();
}

static void message(const char *title, const char *text) {
    frame(&bottom, APP_NAME);
    uiText(false, title, 16, 36, 224, 2);
    uiText(false, text, 16, 72, 224, 6);
    symbolButton(8, 168, 240, 24, 2, UI_BACK, "Back");
    while (!quitting) {
        int key = waitKey();
        if (key & KEY_TOUCH) {
            touchPosition touch = lastTouch;
            if (touch.py >= 168) key |= KEY_A;
        }
        if (key & KEY_START) { quitting = true; break; }
        if (key & (KEY_A | KEY_B)) break;
    }
}

static bool inputField(const char *title, char *value, size_t capacity, bool secret) {
    char edited[96];
    snprintf(edited, sizeof(edited), "%s", value);
    frame(&bottom, title);
    uiPanel(false, 8, 28, 240, 24, 3);
    uiCaption(false, secret ? "START Save / B Back / R Show" :
        !strcmp(title, "SEARCH LIBRARY") ? "START Search / B Back / X Clear" : "START Save / B Back / X Clear",
        16, 56, 224, 6);
    uiPresent();
    keyboardShow();
#ifdef ROMSFINDER_DISPLAY_TEST
    Keyboard *testKeyboard = keyboardGetDefault();
    assert(testKeyboard->grid_height == 24 && testKeyboard->offset_y == -72);
    const KeyMap *testLayout = testKeyboard->mappings[testKeyboard->state];
    for (int row = 0; row < 5; row++) for (int column = 0; column < 32; column++)
        assert(keyboardGetKey(column * 8 + 4, 72 + row * 24 + 12) == testLayout->keymap[row * 32 + column]);
    assert(keyboardGetKey(128, 60) == NOKEY);
#endif
    bool accepted = false, redraw = true, showing = false;
    while (pmMainLoop()) {
        swiWaitForVBlank(); scanKeys();
        clientTick(false);
        int keys = keysDown();
        int character = keyboardUpdate();
        uiKeyboardSync();
        bool showNow = secret && (keysHeld() & KEY_R);
        if (showNow != showing) { showing = showNow; redraw = true; }
        size_t length = strlen(edited);
        if (keys & KEY_B) break;
        if ((keys & KEY_START) || character == DVK_ENTER) { accepted = true; break; }
        if (keys & KEY_X) { edited[0] = 0; redraw = true; }
        else if ((keys & KEY_Y) || character == DVK_BACKSPACE) {
            if (length) edited[length - 1] = 0;
            redraw = true;
        } else if (character >= 32 && character <= 126 && length + 1 < capacity && length + 1 < sizeof(edited)) {
            edited[length] = character; edited[length + 1] = 0; redraw = true;
        }
        if (redraw) {
            uiPanel(false, 8, 28, 240, 24, 3);
            char display[sizeof(edited)];
            snprintf(display, sizeof(display), "%s", edited);
            if (secret && !showing) memset(display, '*', strlen(display));
            const char *visible = display;
            // Scroll by actual glyph advances, leaving room for the caret.
            while (*visible && uiTextWidth(visible) > 216) visible++;
            unsigned visibleWidth = uiTextWidth(visible);
            uiText(false, visible, 16, 32, 220, 1);
            uiPanel(false, 16 + visibleWidth, 32, 2, 14, 4);
            redraw = false;
            uiPresent();
            memset(display, 0, sizeof(display));
        }
    }
    keyboardHide();
    if (accepted) snprintf(value, capacity, "%s", edited);
    memset(edited, 0, sizeof(edited));
    return accepted;
}

static bool inputText(const char *title, char *value, size_t capacity) {
    return inputField(title, value, capacity, false);
}

static void loadSettings(void) {
    FILE *file = fopen(CONFIG_PATH, "r");
    if (!file) return;
    bool ok = settingsRead(file, &settings, lastError, sizeof(lastError));
    fclose(file);
    if (!ok) message("SD configuration", lastError);
}

static bool saveSettings(const Settings *options) {
    if (mkdir("/_nds", 0777) && errno != EEXIST) return false;
    FILE *file = fopen(CONFIG_PATH ".tmp", "w");
    if (!file) return false;
    bool ok = settingsWrite(file, options);
    if (fclose(file)) ok = false;
    if (ok && rename(CONFIG_PATH ".tmp", CONFIG_PATH) != 0) {
        // FAT rename does not always replace an existing destination.
        remove(CONFIG_PATH);
        ok = rename(CONFIG_PATH ".tmp", CONFIG_PATH) == 0;
    }
    return ok;
}

static bool saveDownloadQueue(const DownloadQueue *queue, void *context) {
    (void)context;
    if (mkdir("/_nds", 0777) && errno != EEXIST) goto failed;
    FILE *file = fopen(QUEUE_PATH ".tmp", "w");
    if (!file) goto failed;
    bool ok = queueWrite(file, queue);
    if (fclose(file)) ok = false;
    if (!ok) goto failed;
    struct stat info;
    bool hadQueue = stat(QUEUE_PATH, &info) == 0;
    if (hadQueue) {
        remove(QUEUE_PATH ".bak");
        if (rename(QUEUE_PATH, QUEUE_PATH ".bak")) goto failed;
    }
    if (rename(QUEUE_PATH ".tmp", QUEUE_PATH)) {
        if (hadQueue) rename(QUEUE_PATH ".bak", QUEUE_PATH);
        goto failed;
    }
    return true;
failed:
    strcpy(lastError, "Could not save the queue to SD. Completed games are kept.");
    return false;
}

static void loadDownloadQueue(void) {
    static const char *paths[] = { QUEUE_PATH, QUEUE_PATH ".tmp", QUEUE_PATH ".bak" };
    bool found = false;
    for (unsigned i = 0; i < 3; i++) {
        FILE *file = fopen(paths[i], "r");
        if (!file) continue;
        found = true;
        bool ok = queueRead(file, &downloads);
        fclose(file);
        if (ok) return;
    }
    if (found) message("Queue could not be loaded", "The saved queue is invalid. Game files and Wi-Fi settings are unchanged.");
}

static void enqueueGame(const Game *game) {
    if (!queueAdd(&downloads, game)) {
        snprintf(browserNotice, sizeof(browserNotice), "%s", downloads.count == QUEUE_CAPACITY ? "Queue full (32 games)" : "Already in queue");
        return;
    }
    if (!saveDownloadQueue(&downloads, NULL)) {
        downloads.count--;
        message("Queue not saved", lastError);
        return;
    }
    snprintf(browserNotice, sizeof(browserNotice), "Added to queue / %u waiting", downloads.count);
}

static bool configureServer(void) {
    char host[64], port[8];
    snprintf(host, sizeof(host), "%s", settings.host);
    snprintf(port, sizeof(port), "%u", settings.port);
    if (!inputText("PC ADDRESS / IPv4", host, sizeof(host))) return false;
    struct in_addr address;
    if (!inet_aton(host, &address)) { message("Settings", "Enter your PC's IPv4 address. Do not include http://."); return false; }
    if (!inputText("PC PORT / DEFAULT 3000", port, sizeof(port))) return false;
    char *end;
    unsigned long p = strtoul(port, &end, 10);
    if (!*port || *end || p < 1 || p > 65535) { message("Settings", "Port must be between 1 and 65535."); return false; }
    Settings updated = settings;
    snprintf(updated.host, sizeof(updated.host), "%s", host);
    updated.port = p;
    if (!saveSettings(&updated)) { message("SD write error", "Could not save settings to SD."); return false; }
    settings = updated;
    return true;
}

static bool configureWifi(void) {
    Settings updated = settings;
    updated.wifiMode = WIFI_WPA2;
    if (!inputText("WI-FI NETWORK / SSID", updated.ssid, sizeof(updated.ssid))) return false;
    if (!inputField("WI-FI PASSWORD / WPA2", updated.password, sizeof(updated.password), true)) return false;
    if (!wifiSettingsValid(&updated, lastError, sizeof(lastError))) { message("WPA2 settings", lastError); return false; }
    if (!saveSettings(&updated)) { message("SD write error", "Could not save the WPA2 profile to SD."); return false; }
    settings = updated;
    return true;
}

typedef enum { CONFIG_NONE, CONFIG_SERVER, CONFIG_WIFI, CONFIG_FILTER, CONFIG_UPDATE } ConfigChange;

static void drawSettings(void) {
    drawTopHeading("Settings");
    uiSymbol(true, 16, 50, UI_WIFI, clientRegistered() ? 5 : 6);
    uiHeading(true, clientRegistered() ? "Connected" : "Offline", 44, 44, 196);
    uiText(true, *settings.ssid ? settings.ssid : "System Wi-Fi profiles", 16, 78, 224, 2);
    char address[96];
    snprintf(address, sizeof(address), "%s:%u", settings.host, settings.port);
    uiCaption(true, address, 16, 118, 224, 6);
    uiCaption(true, clientConnectionDetail(), 16, 136, 224, 6);
    struct in_addr ip = { .s_addr = wifiInitialized ? Wifi_GetIP() : 0 };
    snprintf(address, sizeof(address), "IP %s", inet_ntoa(ip));
    uiCaption(true, address, 16, 156, 224, 6);
    snprintf(address, sizeof(address), "MAC %s  /  " APP_VERSION, clientMac());
    uiCaption(true, address, 16, 176, 224, 14);
    frame(&bottom, "Settings");
    uiButton(216, 0, 32, 26, 2); uiSymbol(false, 224, 6, UI_BACK, 6);
    symbolButton(8, 32, 240, 28, settingsFocus == 0 ? 8 : 2, UI_WIFI, "Wi-Fi network");
    symbolButton(8, 66, 240, 28, settingsFocus == 1 ? 8 : 2, UI_SERVER, "Server address");
    symbolButton(8, 100, 116, 28, settingsFocus == 2 ? 8 : 2, UI_SETTINGS, "System Wi-Fi");
    symbolButton(132, 100, 116, 28, settingsFocus == 3 ? 8 : 2, UI_UPDATE, "App update");
    symbolButton(8, 134, 116, 26, settingsFocus == 4 ? 8 : 2, UI_REFRESH, "Reconnect");
    symbolButton(132, 134, 116, 26, settingsFocus == 5 ? 8 : 2, UI_FILTER, "Filter");
    drawNavigation(3);
    settingsVisible = true;
    uiPresent();
}

static ConfigChange configure(void) {
    while (!quitting) {
        drawSettings();
        int key = waitKey();
        if (key & KEY_UP) { settingsFocus = (settingsFocus + 5) % 6; continue; }
        if (key & KEY_DOWN) { settingsFocus = (settingsFocus + 1) % 6; continue; }
        if (key & (KEY_LEFT | KEY_RIGHT) && settingsFocus >= 2) { settingsFocus ^= 1; continue; }
        if (key & KEY_SELECT) {
            navigationRequest = selectNavigation(3);
            if (navigationRequest) return CONFIG_NONE;
            continue;
        }
        if (key & KEY_A && !(key & KEY_TOUCH)) {
            static const int actions[] = {KEY_A, KEY_X, KEY_Y, KEY_L, KEY_START, KEY_R};
            key = (key & ~KEY_A) | actions[settingsFocus];
        }
        if (key & KEY_TOUCH) {
            touchPosition touch = lastTouch;
            if (touch.py >= 164) {
                if (touch.px < 192) { navigationRequest = navigationAt(touch.px); return CONFIG_NONE; }
                continue;
            }
            if (touch.py < 28) key |= KEY_B;
            else if (touch.py >= 32 && touch.py < 60) key |= KEY_A;
            else if (touch.py >= 66 && touch.py < 94) key |= KEY_X;
            else if (touch.py >= 100 && touch.py < 128) key |= touch.px < 128 ? KEY_Y : KEY_L;
            else if (touch.py >= 134 && touch.py < 160) key |= touch.px < 128 ? KEY_START : KEY_R;
        }
        if ((key & KEY_START) && (keysHeld() & KEY_L)) { quitting = true; return CONFIG_NONE; }
        if (key & KEY_B) return CONFIG_NONE;
        if (key & KEY_L) return CONFIG_UPDATE;
        if (key & KEY_START) return CONFIG_WIFI;
        if (key & KEY_R) {
            regionIndex = (regionIndex + 1) % filterCount();
            return CONFIG_FILTER;
        }
        if ((key & KEY_A) && configureWifi()) return CONFIG_WIFI;
        if ((key & KEY_X) && configureServer()) return CONFIG_SERVER;
        if (key & KEY_Y) {
            Settings updated = settings;
            updated.wifiMode = WIFI_FIRMWARE;
            if (!saveSettings(&updated)) { message("SD write error", "Could not save settings to SD."); continue; }
            settings = updated;
            return CONFIG_WIFI;
        }
    }
    return CONFIG_NONE;
}

static void registerClient(void) {
    savesConnect();
    libraryConnect(appPath);
    clientConnect();
    startup(2, "Registering this DSi...");
    bool registered = clientRegister();
    startup(2, registered ? "DSi registered with PC" : "Registration will retry");
}

static bool connectWifi(bool reconnect) {
    clientClose();
    coverPausePrefetch();
    lastError[0] = 0;
    if (!wifiSettingsValid(&settings, lastError, sizeof(lastError))) return false;
    drawBrand();
    startup(1, settings.wifiMode == WIFI_WPA2 ? "Connecting with WPA2..." : "Using system profiles...");
    if (!wifiInitialized) {
        wifiInitialized = Wifi_InitDefault(INIT_ONLY);
        if (!wifiInitialized) { strcpy(lastError, "Could not initialize Wi-Fi"); return false; }
    }
    if (!reconnect && Wifi_AssocStatus() == ASSOCSTATUS_ASSOCIATED) { registerClient(); return true; }
    if (!wifiBegin(&settings, lastError, sizeof(lastError))) return false;
    for (unsigned i = 0; i < 60 * 60; i++) {
        if (!appYield()) { wlmgrStop(); strcpy(lastError, "Wi-Fi connection cancelled"); return false; }
        int state = Wifi_AssocStatus();
        if (state == ASSOCSTATUS_ASSOCIATED) { registerClient(); return true; }
        if (i > 60 && state == ASSOCSTATUS_CANNOTCONNECT) break;
        if (i % 30 == 0) {
            const char *detail = state == ASSOCSTATUS_SEARCHING ? "Finding your Wi-Fi..." :
                state == ASSOCSTATUS_ACQUIRINGDHCP ? "Obtaining IP address..." :
                settings.wifiMode == WIFI_WPA2 ? "Authenticating WPA2..." : "Connecting profile...";
            startup(1, detail);
        }
    }
    wlmgrStop();
    strcpy(lastError, settings.wifiMode == WIFI_WPA2 ?
        "WPA2 failed. Check SSID, password and 2.4 GHz WPA2 / AES. SELECT: Wi-Fi setup or reconnect." :
        "Wi-Fi failed. SELECT: set up your WPA2 network. SELECT > START: reconnect.");
    return false;
}

static bool fetchPage(int requestedPage) {
    coverPausePrefetch();
    char encoded[244], path[512];
    lastError[0] = 0;
    if (browserVisible) {
        uiPanel(true, 16, 172, 224, 20, 10);
        uiCenteredCaption(true, "Loading library...", 16, 176, 224, 6);
        uiPresent();
    } else if (startupVisible) startup(2, "Loading your catalog...");
    else progress("Loading library", "Fetching search results from your PC...", 0, 0);
    urlEncode(query, encoded, sizeof(encoded));
    bool others = activeSystem == SYSTEM_OTHERS;
    snprintf(path, sizeof(path), "/api/dsi/catalog?system=%s&q=%s&page=%d&%s=%s", systemKey(activeSystem), encoded, requestedPage,
        others ? "type" : "region", others ? othersTypes[regionIndex] : regions[regionIndex]);
    if (others && regionIndex == 3) {
        size_t used = strlen(path);
        snprintf(path + used, sizeof(path) - used, "&style=%s&favorites=%u", themeStyles[themeStyle], themeFavoritesOnly ? 1 : 0);
    }
    if (!httpText("GET", path, response, sizeof(response))) return false;

    Game newGames[PAGE_SIZE] = {0};
    int newCount, newTotal, newPage, newPages;
    if (!parseCatalogForSystem(response, newGames, &newCount, &newTotal, &newPage, &newPages, activeSystem)) {
        strcpy(lastError, "Invalid catalog response. Update the PC server."); return false;
    }
    memcpy(games, newGames, sizeof(games));
    count = newCount; total = newTotal; page = newPage; pages = newPages; selected = 0;
    coverPage(games, count, selected);
    return true;
}

// Picks a random NDS game regardless of the active system or current filters.
static bool fetchRandomGame(void) {
    coverPausePrefetch();
    // Static, not stack: a second PAGE_SIZE buffer would blow the stack-usage budget.
    static Game probeScratch[PAGE_SIZE];
    char path[96];
    lastError[0] = 0;
    if (browserVisible) {
        uiPanel(false, 8, 124, 240, 36, 2);
        uiSymbol(false, 16, 134, UI_RANDOM, 5);
        uiText(false, "Picking a random game...", 40, 134, 200, 1);
        uiPresent();
    } else progress("Loading library", "Picking a random game from your PC...", 0, 0);
    snprintf(path, sizeof(path), "/api/dsi/catalog?system=nds&q=&page=0&region=");
    if (!httpText("GET", path, response, sizeof(response))) return false;
    int probeCount, probeTotal, probePage, probePages;
    if (!parseCatalogForSystem(response, probeScratch, &probeCount, &probeTotal, &probePage, &probePages, SYSTEM_NDS)) {
        strcpy(lastError, "Invalid catalog response. Update the PC server."); return false;
    }
    if (probeTotal <= 0) { strcpy(lastError, "No NDS games available for a random pick."); return false; }

    snprintf(path, sizeof(path), "/api/dsi/catalog?system=nds&q=&page=%d&region=", rand() % probePages);
    if (!httpText("GET", path, response, sizeof(response))) return false;
    int newCount, newTotal, newPage, newPages;
    if (!parseCatalogForSystem(response, probeScratch, &newCount, &newTotal, &newPage, &newPages, SYSTEM_NDS) || !newCount) {
        strcpy(lastError, "Invalid catalog response. Update the PC server."); return false;
    }
    memcpy(games, probeScratch, sizeof(games));
    activeSystem = SYSTEM_NDS; query[0] = 0; regionIndex = 0;
    count = newCount; total = newTotal; page = newPage; pages = newPages; selected = rand() % count;
    coverPage(games, count, selected);
    return true;
}

static void drawBrowser(void) {
    if (count) drawFocusedGame(&games[selected], browserNotice);
    else {
        drawTopHeading(APP_NAME);
        uiSymbol(true, 120, 56, UI_CATALOG, 4);
        uiText(true, "No entries found", 16, 88, 224, 1);
        uiText(true, clientRegistered() ? "Try another search or category." : "Library server unavailable.", 16, 112, 224, 2);
    }
    if (count) {
        bool known;
        const u16 *left = count > 2 ? coverFind(&games[(selected + count - 1) % count], &known) : NULL;
        const u16 *right = count > 1 ? coverFind(&games[(selected + 1) % count], &known) : NULL;
        uiTopNeighbours(left, right);
    }
    if (!*browserNotice && count) {
        static const char *regionTags[] = {"All", "EU", "DE", "US", "JP", "World"};
        static const char *categoryTags[] = {"All", "Apps", "Uploads", "Themes", "Media"};
        const char *tag = (activeSystem == SYSTEM_OTHERS ? categoryTags : regionTags)[regionIndex];
        char detail[32]; snprintf(detail, sizeof(detail), "%s %d/%d", tag, page + 1, pages);
        uiPanel(true, 96, 0, 116, 24, 1);
        uiCaption(true, detail, 100, 5, 108, 6);
    }
    clear(&bottom);
    for (unsigned slot = 0; slot < 2; slot++) {
        RomSystem tab = visibleSystem(slot);
        uiButton(slot ? 72 : 8, 2, 60, 24, activeSystem == tab ? 8 : 2);
        uiText(false, systemLabel(tab), slot ? 80 : 16, 5, 44, 1);
        if (activeSystem == tab) uiPanel(false, slot ? 80 : 16, 24, 44, 2, 4);
    }
    uiButton(136, 2, 32, 24, 2);
    uiSymbol(false, 144, 6, UI_RANDOM, 6);
    uiButton(172, 2, 32, 24, 2); uiSymbol(false, 180, 6, UI_SEARCH, 6);
    uiButton(208, 2, 40, 24, 2);
    uiSymbol(false, 220, 6, UI_FILTER, 6);
    int first = selected < 6 ? 0 : selected - 5;
    for (int i = first; i < count && i < first + 6; i++) {
        int y = 30 + (i - first) * 22;
        uiListRow(y, games[i].title, i + 1, i == selected);
    }
    if (!count) uiText(false, "No results", 16, 72, 224, 1);
    drawNavigation(0);
    browserVisible = true;
    uiPresent();
}

static void chooseThemeStyle(void) {
    unsigned choice = themeStyle;
    while (!quitting) {
        frame(&bottom, "Menu style");
        for (unsigned index = 0; index < 5; index++) {
            int y = 28 + index * 22;
            uiListRow(y, themeStyleLabels[index], index + 1, index == choice);
        }
        symbolButton(8, 140, 240, 24, 2, UI_FAVORITE, themeFavoritesOnly ? "Favorites only: On" : "Favorites only: Off");
        symbolButton(8, 168, 240, 24, 2, UI_BACK, "Back");
        int key = waitKey();
        if (key & KEY_TOUCH) {
            if (lastTouch.py >= 168) key |= KEY_B;
            else if (lastTouch.py >= 140) key |= KEY_X;
            else if (lastTouch.py >= 28 && lastTouch.py < 138) { choice = (lastTouch.py - 28) / 22; key |= KEY_A; }
        }
        if (key & KEY_UP) choice = (choice + 4) % 5;
        if (key & KEY_DOWN) choice = (choice + 1) % 5;
        if (key & KEY_X) themeFavoritesOnly = !themeFavoritesOnly;
        if (key & (KEY_A | KEY_B)) { themeStyle = choice; return; }
    }
}

static bool chooseRegion(void) {
    unsigned choice = regionIndex;
    unsigned options = filterCount();
    while (!quitting) {
        frame(&bottom, activeSystem == SYSTEM_OTHERS ? "Category" : "Region");
        for (unsigned i = 0; i < options; i++) {
            int y = 28 + i * 22;
            uiListRow(y, filterLabel(i), i + 1, i == choice);
        }
        symbolButton(8, 168, 240, 24, 2, UI_BACK, "Back");
        int key = waitKey();
        if (key & KEY_TOUCH) {
            if (lastTouch.py >= 168) key |= KEY_B;
            else if (lastTouch.py >= 28 && lastTouch.py < 28 + (int)options * 22) { choice = (lastTouch.py - 28) / 22; key |= KEY_A; }
        }
        if (key & (KEY_B | KEY_SELECT)) return false;
        if (key & KEY_UP) choice = (choice + options - 1) % options;
        if (key & KEY_DOWN) choice = (choice + 1) % options;
        if (key & (KEY_A | KEY_START)) {
            bool changed = choice != (unsigned)regionIndex;
            regionIndex = choice;
            if (activeSystem == SYSTEM_OTHERS && choice == 3) { chooseThemeStyle(); return true; }
            return changed;
        }
    }
    return false;
}

static bool checkFile(const char *file, uint32_t expectedSize, uint32_t expectedCrc) {
    FILE *input = fopen(file, "rb");
    if (!input) return false;
    uint32_t size = 0, crc = 0xffffffffU;
    bool ok = true;
    size_t n;
    while ((n = fread(fileBuffer, 1, sizeof(fileBuffer), input))) {
        crc = crcUpdate(crc, fileBuffer, n); size += n;
        if ((size & 65535) == 0 && !appYield()) { ok = false; break; }
    }
    if (ferror(input)) ok = false;
    fclose(input);
    return ok && size == expectedSize && (crc ^ 0xffffffffU) == expectedCrc;
}

static bool transfer(const Game *game, const Job *job, const char *updateDestination) {
    char destination[200], partial[232], path[160];
    bool update = updateDestination != NULL;
    size_t nameLength = strlen(job->name);
    bool theme = !update && game->system == SYSTEM_OTHERS && nameLength > 6 && !strcmp(job->name + nameLength - 6, ".theme");
    bool folder = !update && game->system == SYSTEM_OTHERS && nameLength > 7 && !strcmp(job->name + nameLength - 7, ".folder");
    const char *media = !update && game->system == SYSTEM_OTHERS ? mediaDirectory(job->name) : NULL;
    const char *directory = update ? "/" : media ? media : downloadDirectory(game->system);
    if (update) {
        snprintf(destination, sizeof(destination), "%s", updateDestination);
        snprintf(partial, sizeof(partial), "%s.update-%08lx.part", destination, (unsigned long)job->crc);
    } else {
        snprintf(destination, sizeof(destination), "%s/%s", directory, job->name);
        if (theme || folder) snprintf(partial, sizeof(partial), "%s.%08lx.part", destination, (unsigned long)job->crc);
        else snprintf(partial, sizeof(partial), "%s.part", destination);
    }
    if (!update && ((!media && mkdir("/roms", 0777) && errno != EEXIST) || (mkdir(directory, 0777) && errno != EEXIST))) {
        strcpy(lastError, "Could not create the download folder"); return false;
    }
    struct stat info;
    if (!stat(destination, &info)) {
        progress(game->title, "Checking the existing file...", 0, 0);
        if (checkFile(destination, job->size, job->crc)) {
            if (theme || folder) {
                bool installed = folder ? installFolder(destination, lastError, sizeof(lastError)) : installTheme(destination, lastError, sizeof(lastError));
                if (installed) { remove(destination); if (!queueRunning && !clientActive()) message(folder ? "Folder installed" : "Theme installed", lastError); }
                return installed;
            }
            if (!update && !queueRunning && !clientActive()) message("Already on your SD card", destination);
            return true;
        }
        if (*lastError) return false;
        if (!update) {
            strcpy(lastError, "A different file already uses this name. Rename or remove it using your PC.");
            return false;
        }
    }
    uint32_t offset = 0, crc = 0xffffffffU;
    if (!stat(partial, &info)) {
        if (info.st_size < 0 || (uint64_t)info.st_size > job->size) {
            if (remove(partial)) { strcpy(lastError, "Could not remove the invalid partial file"); return false; }
        } else offset = (uint32_t)info.st_size;
    }
    struct statvfs space;
    if (statvfs(directory, &space) == 0 &&
        (uint64_t)space.f_bavail * space.f_frsize < (uint64_t)(job->size - offset) + 65536) {
        strcpy(lastError, "Not enough free space on your SD card"); return false;
    }
    if (offset) {
        progress(game->title, "Checking your partial download...", 0, offset);
        FILE *previous = fopen(partial, "rb");
        if (!previous) { strcpy(lastError, "Could not open the partial download"); return false; }
        uint32_t read = 0;
        while (read < offset) {
            size_t n = fread(fileBuffer, 1, sizeof(fileBuffer), previous);
            if (!n) break;
            crc = crcUpdate(crc, fileBuffer, n); read += n;
            if (!appYield()) { fclose(previous); return false; }
        }
        bool ok = read == offset && !ferror(previous);
        fclose(previous);
        if (!ok) { strcpy(lastError, "Could not read the partial download"); return false; }
    }
    FILE *output = fopen(partial, offset ? "ab" : "wb");
    if (!output) { strcpy(lastError, "Could not write to your SD card"); return false; }
    bool ok = true;
    uint32_t received = offset;
    if (offset < job->size) {
        Http *http = &downloadHttp;
        if (update) snprintf(path, sizeof(path), "/api/dsi/update?file=1&crc=%08lx&size=%lu", (unsigned long)job->crc, (unsigned long)job->size);
        else snprintf(path, sizeof(path), "/api/dsi/file?system=%s&id=%s", systemKey(game->system), game->id);
        progress(game->title, offset ? "Resuming your SD download..." : "Downloading to your SD card...", offset, job->size);
        if (!httpOpen(http, "GET", path, offset)) { fclose(output); return false; }
        if (http->status != (offset ? 206 : 200) || http->length != job->size - offset ||
            (offset && (http->rangeStart != offset || http->rangeTotal != job->size))) {
            snprintf(lastError, 192, "Invalid download response (HTTP %d)", http->status);
            httpClose(http); fclose(output); return false;
        }
        time_t lastDraw = 0, lastFlush = time(NULL);
        time_t started = time(NULL);
        while (received < job->size) {
            size_t want = job->size - received;
            if (want > sizeof(fileBuffer)) want = sizeof(fileBuffer);
            int n = httpRead(http, fileBuffer, want);
            if (n <= 0) {
                if (!*lastError) strcpy(lastError, "Download interrupted. Press A to resume.");
                ok = false; break;
            }
            if (fwrite(fileBuffer, 1, n, output) != (size_t)n) {
                strcpy(lastError, "SD write failed. Is the card full?"); ok = false; break;
            }
            crc = crcUpdate(crc, fileBuffer, n); received += n;
            clientProgress(received, job->size, "Downloading to SD");
            if (!appPoll()) { ok = false; break; }
            time_t now = time(NULL);
            if (now != lastDraw) {
                char text[128];
                unsigned long seconds = now > started ? (unsigned long)(now - started) : 1;
                unsigned long speed = ((received - offset) / 1024) / seconds;
                snprintf(text, sizeof(text), "SD download: %lu KiB/s", speed);
                progress(game->title, text, received, job->size); lastDraw = now;
            }
            if (now - lastFlush >= 5) {
                if (fflush(output)) { strcpy(lastError, "SD write error"); ok = false; break; }
                lastFlush = now;
            }
        }
        httpClose(http);
    }
    if (fclose(output)) { strcpy(lastError, "Could not finish writing the SD file"); ok = false; }
    if (!ok) return false;
    if ((crc ^ 0xffffffffU) != job->crc) {
        remove(partial);
        strcpy(lastError, "CRC32 mismatch. Partial file removed. Press A to download again."); return false;
    }
    if (update) return installAppUpdate(partial, destination, lastError, sizeof(lastError));
    if (theme || folder) {
        bool installed = folder ? installFolder(partial, lastError, sizeof(lastError)) : installTheme(partial, lastError, sizeof(lastError));
        if (installed) { remove(partial); if (!queueRunning && !clientActive()) message(folder ? "Folder installed" : "Theme installed", lastError); }
        return installed;
    }
    if (rename(partial, destination)) { strcpy(lastError, "Could not rename the completed file. Press A to retry."); return false; }
    if (!queueRunning && !clientActive()) {
        char text[192];
        snprintf(text, sizeof(text), media ? "Saved to %s/. START: exit app." : "Saved to %s/. Return to TWiLight Menu++ to start your game. START: exit app.", directory);
        message("Download complete", text);
    }
    return true;
}

static bool prepareAndTransfer(const Game *game, void *context) {
    (void)context;
    lastError[0] = 0;
    transferSystem = game->system;
    Job job;
    char path[128];
    snprintf(path, sizeof(path), "/api/dsi/prepare?system=%s&id=%s", systemKey(game->system), game->id);
    progress(game->title, "Starting preparation on your PC...", 0, 0);
    for (;;) {
        if (httpText("POST", path, response, sizeof(response))) break;
        if (!clientActive() || strncmp(lastError, "HTTP 409:", 9)) return false;
        progress(game->title, "Waiting for another PC preparation...", 0, 0);
        for (unsigned i = 0; i < 120; i++) if (!appYield()) return false;
    }
    for (;;) {
        if (!parseJobForSystem(response, &job, game->system)) { strcpy(lastError, "Invalid status or wrong file type. Update the PC server."); return false; }
        if (!strcmp(job.state, "ready")) break;
        if (!strcmp(job.state, "error")) { snprintf(lastError, 192, "%s", job.message); return false; }
        progress(game->title, job.message, job.received, job.total);
        for (int i = 0; i < 120; i++) if (!appYield()) return false;
        snprintf(path, sizeof(path), "/api/dsi/status?system=%s&id=%s", systemKey(game->system), game->id);
        if (!httpText("GET", path, response, sizeof(response))) return false;
    }
    return transfer(game, &job, NULL);
}

static void updateApp(void) {
    coverPausePrefetch();
    lastError[0] = 0;
    progress("DSiDrop update", "Checking the PC server build...", 0, 0);
    if (!httpText("GET", "/api/dsi/update", response, sizeof(response))) { message("Update", lastError); return; }
    Job job;
    if (!parseJobForSystem(response, &job, SYSTEM_NDS) || strcmp(job.state, "ready") || strcmp(job.name, "DSiDrop.nds") || job.size > 16U * 1024U * 1024U) {
        message("Update", "Invalid update response. Update your PC server first."); return;
    }
    if (checkFile(appPath, job.size, job.crc)) {
        message("Already up to date", "This file already matches the build provided by your PC server."); return;
    }
    if (*lastError || quitting) return;
    frame(&bottom, "UPDATE DSIDROP");
    uiText(false, "Running: " APP_VERSION, 16, 32, 224, 1);
    char buildLabel[32];
    snprintf(buildLabel, sizeof(buildLabel), "PC build: %08lx", (unsigned long)job.crc);
    uiCaption(false, buildLabel, 16, 50, 224, 6);
    uiText(false, "Install to:", 16, 68, 224, 1);
    uiText(false, appPath, 16, 84, 224, 2);
    uiCaption(false, appPathKnown ? "Old app saved as .nds.bak" : "No launch path: SD root copy", 16, 124, 224, 6);
    symbolButton(8, 144, 240, 24, 4, UI_UPDATE, "Install update");
    symbolButton(8, 168, 240, 24, 2, UI_CLOSE, "Cancel");
    for (;;) {
        int key = waitKey();
        if (quitting) return;
        if (key & KEY_TOUCH) {
            if (lastTouch.py >= 144 && lastTouch.py < 168) key |= KEY_A;
            else if (lastTouch.py >= 168) key |= KEY_B;
        }
        if (key & KEY_B) return;
        if (key & KEY_A) break;
    }
    Game game = { .system = SYSTEM_NDS };
    strcpy(game.title, "DSiDrop update");
    transferSystem = SYSTEM_NDS;
    if (!transfer(&game, &job, appPath)) { if (!quitting) message("Update failed", lastError); return; }
    message("Update installed", "Close this message to exit, then reopen DSiDrop from TWiLight Menu++. The previous build is kept as .nds.bak.");
    quitting = true;
}

static void findAppPath(int argc, char **argv) {
    if (argc < 1 || !argv || !argv[0]) return;
    const char *extension = strrchr(argv[0], '.');
    if (!extension || strcasecmp(extension, ".nds")) return;
    char candidate[sizeof(appPath)], current[sizeof(appPath)];
    int length;
    if (argv[0][0] == '/' || strchr(argv[0], ':')) length = snprintf(candidate, sizeof(candidate), "%s", argv[0]);
    else {
        if (!getcwd(current, sizeof(current))) return;
        length = snprintf(candidate, sizeof(candidate), "%s/%s", current, argv[0]);
    }
    struct stat info;
    if (length < 0 || (size_t)length >= sizeof(candidate) || stat(candidate, &info) || !S_ISREG(info.st_mode)) return;
    strcpy(appPath, candidate);
    appPathKnown = true;
}

static bool remoteTransfer(void) {
    Game game;
    if (!clientTake(&game)) return false;
    coverPausePrefetch();
    bool ok = prepareAndTransfer(&game, NULL);
    bool canceled = quitting || strstr(lastError, "Paused") || strstr(lastError, "cancelled");
    clientFinish(ok, canceled, ok ? "Saved to SD" : lastError);
    snprintf(browserNotice, sizeof(browserNotice), "%s", ok ? "Web download saved to SD" : canceled ? "Web queue paused on PC" : "Web download failed; check Devices");
    return true;
}

static bool remoteLibrary(void) {
    if (clientPending() || clientActive()) return false;
    LibraryCommand command;
    if (!libraryPoll(&command)) return false;
    coverPausePrefetch();
    bool ok = libraryExecute(&command);
    libraryFinish(&command, ok);
    snprintf(browserNotice, sizeof(browserNotice), "%s", ok ? command.remove ? "ROM deleted; saves preserved" : "Games list updated on PC" : "Library operation failed; check Devices");
    return true;
}

static bool remoteSaves(void) {
    if (clientPending() || clientActive()) return false;
    SaveCommand command;
    if (!savesPoll(&command)) return false;
    coverPausePrefetch();
    bool ok = savesExecute(&command);
    savesFinish(&command, ok, lastError);
    snprintf(browserNotice, sizeof(browserNotice), "%s", ok ? command.restore ? "Save restored" : "Saves backed up to PC" : "Save operation failed; check Backups");
    return true;
}

static int navigationAt(int x) {
    static const int actions[] = { ACTION_SELECT, ACTION_SD, KEY_START, ACTION_SETUP };
    return actions[x / 64];
}

static int selectNavigation(unsigned active) {
    static const char *labels[] = {"Catalog", "SD card", "Transfers", "Settings"};
    unsigned focus = active;
    while (!quitting) {
        frame(&bottom, "Explore");
        for (unsigned index = 0; index < 4; index++) {
            int y = 32 + index * 32;
            uiButton(8, y, 240, 28, index == focus ? 8 : 2);
            uiSymbol(false, 20, y + 6, (UiSymbol)index, index == focus ? 5 : 6);
            uiText(false, labels[index], 52, y + 5, 184, 1);
        }
        drawNavigation(active);
        int key = waitKey();
        if (key & (KEY_UP | KEY_LEFT)) focus = (focus + 3) % 4;
        else if (key & (KEY_DOWN | KEY_RIGHT)) focus = (focus + 1) % 4;
        if (key & KEY_TOUCH) {
            if (lastTouch.py >= 164) return navigationAt(lastTouch.px);
            if (lastTouch.py >= 32 && lastTouch.py < 160) {
                focus = (lastTouch.py - 32) / 32;
                key |= KEY_A;
            }
        }
        if (key & (KEY_B | KEY_SELECT)) return 0;
        if (key & KEY_A) return navigationAt(focus * 64);
    }
    return 0;
}

static bool dashboardNavigation(unsigned active, int key) {
    if (key & KEY_SELECT) {
        navigationRequest = selectNavigation(active);
        return navigationRequest != 0;
    }
    if (key & KEY_START) {
        navigationRequest = KEY_START;
        return true;
    }
    if (!(key & KEY_TOUCH) || lastTouch.py < 164 || lastTouch.px / 64 == (int)active) return false;
    navigationRequest = navigationAt(lastTouch.px);
    return true;
}

static int waitDashboardKey(void) {
    uiPresent();
    while (!quitting && pmMainLoop()) {
        swiWaitForVBlank(); scanKeys();
        int pending = deferredKeys; deferredKeys = 0;
        int key = catalogInput(pending);
        if (key & KEY_TOUCH) {
            if (!(pending & KEY_TOUCH)) touchRead(&lastTouch);
            if (!uiTouchFeedback(lastTouch.px, lastTouch.py)) key &= ~KEY_TOUCH;
        }
        clientTick(!key);
        if (key) return key;
        if (remoteTransfer() || remoteLibrary() || remoteSaves() || coverPrefetchTick(0)) return ACTION_SELECT;
    }
    quitting = true;
    return KEY_B;
}

static bool confirmAction(const char *title, const char *detail) {
    frame(&bottom, title);
    uiSymbol(false, 16, 32, strstr(title, "Delete") ? UI_DELETE : strstr(title, "Transfer") ? UI_TRANSFER : UI_RESTORE, 5);
    uiText(false, detail, 16, 56, 224, 5);
    symbolButton(8, 132, 240, 28, 4, UI_CHECK, "Confirm");
    symbolButton(8, 164, 240, 28, 2, UI_CLOSE, "Cancel");
    while (!quitting) {
        int key = waitKey();
        if (key & KEY_TOUCH) {
            if (lastTouch.py >= 164) key |= KEY_B;
            else if (lastTouch.py >= 132) key |= KEY_A;
        }
        if (key & KEY_B) return false;
        if (key & KEY_A) return true;
    }
    return false;
}

static bool saveServerReady(void) {
    if (!clientRegistered()) {
        if (!connectWifi(false)) { message("Server unavailable", lastError); return false; }
        clientTick(false);
    }
    return true;
}

static bool runSaveRequest(const char *action, const char *path, const char *backup, const SaveTarget *target, bool automatic) {
    if (!saveServerReady()) return false;
    if (!savesRequest(action, path, backup, target, automatic)) { message("Save request failed", lastError); return false; }
    if (!strcmp(action, "backup") || !strcmp(action, "restore")) {
        SaveCommand command;
        if (savesPoll(&command)) {
            bool ok = savesExecute(&command);
            savesFinish(&command, ok, lastError);
            message(ok ? "Save operation complete" : "Save operation failed", ok ?
                command.restore ? "Save restored. Previous save backed up." : "Save backup stored on your server." : lastError);
            return ok;
        }
        message("Save request queued", "The operation will run while DSiDrop remains open.");
    } else if (!strcmp(action, "transfer")) message("Transfer queued", "The destination DSi will back up its current save before restoring this version.");
    return true;
}

static void transferSave(const SaveVersion *version) {
    static SaveTarget targets[PAGE_SIZE];
    unsigned page = 0, selectedTarget = 0, amount = 0, totalTargets = 0;
    bool reload = true;
    while (!quitting && !navigationRequest) {
        if (reload) {
            if (!savesTargets(version, page, targets, &amount, &totalTargets)) { message("Save transfer", lastError); return; }
            selectedTarget = 0; reload = false;
        }
        drawTopHeading("Destination DSi");
        uiSymbol(true, 120, 48, UI_TRANSFER, 4);
        if (amount) {
            uiText(true, targets[selectedTarget].name, 16, 76, 224, 2);
            uiText(true, targets[selectedTarget].path, 16, 112, 224, 4);
        } else uiText(true, "No online DSi with a matching installed game.", 16, 88, 224, 3);
        frame(&bottom, "Transfer save");
        for (unsigned index = 0; index < amount; index++) {
            int y = 30 + index * 22;
            uiListRow(y, targets[index].name, index + 1, index == selectedTarget);
        }
        if (!amount) uiText(false, "No destinations available", 16, 64, 224, 2);
        drawNavigation(1);
        int key = waitDashboardKey();
        if (dashboardNavigation(1, key) || (key & KEY_B)) return;
        if (key & KEY_TOUCH && lastTouch.py >= 30 && lastTouch.py < 162) {
            unsigned index = (lastTouch.py - 30) / 22;
            if (index < amount) { if (index == selectedTarget) key |= KEY_A; selectedTarget = index; }
        }
        if (amount && key & KEY_UP) selectedTarget = (selectedTarget + amount - 1) % amount;
        else if (amount && key & KEY_DOWN) selectedTarget = (selectedTarget + 1) % amount;
        else if (key & KEY_LEFT && page) { page--; reload = true; }
        else if (key & KEY_RIGHT && (page + 1) * 6 < totalTargets) { page++; reload = true; }
        else if (amount && key & KEY_A) {
            char detail[384];
            snprintf(detail, sizeof(detail), "Send this backup to %s?\n%s\nThe destination save is backed up first.", targets[selectedTarget].name, targets[selectedTarget].path);
            if (confirmAction("Transfer save", detail)) {
                runSaveRequest("transfer", version->path, version->id, &targets[selectedTarget], false);
                return;
            }
        }
    }
}

static void versionActions(const SaveVersion *version) {
    unsigned focus = 0;
    while (!quitting && !navigationRequest) {
        frame(&bottom, "Backup actions");
        uiText(false, version->date, 16, 28, 224, 1);
        symbolButton(8, 52, 240, 28, focus == 0 ? 8 : 2, UI_RESTORE, "Restore backup");
        symbolButton(8, 88, 240, 28, focus == 1 ? 8 : 2, UI_TRANSFER, "Transfer to another DSi");
        symbolButton(8, 124, 240, 28, focus == 2 ? 8 : 2, UI_DELETE, "Delete server backup");
        drawNavigation(1);
        int key = waitDashboardKey();
        if (dashboardNavigation(1, key) || key & KEY_B) return;
        if (key & KEY_UP) { focus = (focus + 2) % 3; continue; }
        if (key & KEY_DOWN) { focus = (focus + 1) % 3; continue; }
        if (key & KEY_A && !(key & KEY_TOUCH)) key = (key & ~KEY_A) | (focus == 0 ? KEY_A : focus == 1 ? KEY_Y : KEY_X);
        if (key & KEY_TOUCH) {
            if (lastTouch.py >= 52 && lastTouch.py < 80) key |= KEY_A;
            else if (lastTouch.py >= 88 && lastTouch.py < 116) key |= KEY_Y;
            else if (lastTouch.py >= 124 && lastTouch.py < 152) key |= KEY_X;
        }
        if (key & KEY_A) {
            if (confirmAction("Restore backup", "Replace the current save with this version?\nThe current save is backed up before replacement.")) {
                runSaveRequest("restore", version->path, version->id, NULL, false);
                return;
            }
        } else if (key & KEY_Y) { transferSave(version); return; }
        else if (key & KEY_X && confirmAction("Delete backup", "Delete this server backup permanently?\nThe current save on SD is not changed.")) {
            runSaveRequest("delete", version->path, version->id, NULL, false);
            return;
        }
    }
}

static void backupLocalSave(const SdGame *game) {
    static char paths[4][224];
    unsigned amount = librarySavePaths(game, paths), choice = 0;
    if (!amount) { message("No local save", "No nonempty .sav file was found beside this game or in its saves folder."); return; }
    while (amount > 1 && !quitting && !navigationRequest) {
        frame(&bottom, "Choose save file");
        for (unsigned index = 0; index < amount; index++) {
            int y = 30 + index * 32;
            uiButton(8, y, 240, 30, index == choice ? 8 : 1);
            uiText(false, paths[index], 16, y, 224, 2);
        }
        drawNavigation(1);
        int key = waitDashboardKey();
        if (dashboardNavigation(1, key) || key & KEY_B) return;
        if (key & KEY_UP) choice = (choice + amount - 1) % amount;
        else if (key & KEY_DOWN) choice = (choice + 1) % amount;
        if (key & KEY_TOUCH && lastTouch.py >= 30 && lastTouch.py < 158) {
            unsigned index = (lastTouch.py - 30) / 32;
            if (index < amount) { choice = index; key |= KEY_A; }
        }
        if (key & KEY_A) break;
    }
    if (!quitting && !navigationRequest) runSaveRequest("backup", paths[choice], NULL, NULL, false);
}

static void showNativeSaves(const SdGame *game) {
    static SaveVersion versions[PAGE_SIZE];
    unsigned page = 0, choice = 0, amount = 0, totalVersions = 0;
    bool automatic = false, reload = true;
    if (!saveServerReady()) return;
    while (!quitting && !navigationRequest) {
        if (reload) {
            if (!savesVersions(game->path, page, versions, &amount, &totalVersions, &automatic)) { message("Save backups", lastError); return; }
            if (!amount && page) { page--; continue; }
            choice = 0; reload = false;
        }
        char status[96];
        snprintf(status, sizeof(status), "%u backups / auto backup %s", totalVersions, automatic ? "on" : "off");
        drawFocusedGame(&sdDetail, status);
        frame(&bottom, "Backups");
        uiButton(100, 0, 48, 26, automatic ? 8 : 2); uiText(false, "Auto", 108, 5, 32, 1);
        uiButton(152, 0, 64, 26, 2); uiText(false, "Backup", 158, 5, 52, 1);
        uiButton(220, 0, 28, 26, 2); uiSymbol(false, 226, 6, UI_BACK, 6);
        for (unsigned index = 0; index < amount; index++) {
            int y = 30 + index * 22;
            uiButton(8, y, 240, 21, index == choice ? 8 : 1);
            uiText(false, versions[index].date, 16, y + 2, 150, 1);
            char size[20]; snprintf(size, sizeof(size), "%lu KiB", (unsigned long)((versions[index].size + 1023) / 1024));
            uiText(false, size, 172, y + 2, 68, 1);
        }
        if (!amount) uiText(false, "No server backups yet", 16, 68, 224, 2);
        drawNavigation(1);
        int key = waitDashboardKey();
        if (dashboardNavigation(1, key) || key & KEY_B) return;
        if (key & KEY_TOUCH) {
            if (lastTouch.py < 28) {
                if (lastTouch.px >= 220) return;
                if (lastTouch.px >= 152) key |= KEY_X;
                else if (lastTouch.px >= 100) key |= KEY_Y;
            } else if (lastTouch.py < 162) {
                unsigned index = (lastTouch.py - 30) / 22;
                if (index < amount) { if (index == choice) key |= KEY_A; choice = index; }
            }
        }
        if (amount && key & KEY_UP) choice = (choice + amount - 1) % amount;
        else if (amount && key & KEY_DOWN) choice = (choice + 1) % amount;
        else if (key & KEY_LEFT && page) { page--; reload = true; }
        else if (key & KEY_RIGHT && (page + 1) * 6 < totalVersions) { page++; reload = true; }
        else if (key & KEY_X) { backupLocalSave(game); reload = true; }
        else if (key & KEY_Y) {
            if (runSaveRequest("automatic", NULL, NULL, NULL, !automatic)) reload = true;
        } else if (amount && key & KEY_A) { versionActions(&versions[choice]); reload = true; }
        else if (key & ACTION_SELECT) reload = true;
    }
}

static void selectSdDetail(const SdGame *game) {
    memset(&sdDetail, 0, sizeof(sdDetail));
    strcpy(sdDetail.title, game->title); strcpy(sdDetail.region, "On SD");
    sdDetail.size = game->size; sdDetail.system = game->system;
    if (clientRegistered()) savesGame(game->path, &sdDetail);
    coverPage(&sdDetail, *sdDetail.id ? 1 : 0, 0);
}

static void sdGameActions(unsigned index) {
    const SdGame *game = libraryLocalGame(index);
    if (!game) return;
    unsigned focus = 0;
    while (!quitting && !navigationRequest) {
        frame(&bottom, "On SD");
        symbolButton(8, 40, 240, 32, focus == 0 ? 8 : 2, UI_SAVE, "Save backups");
        symbolButton(8, 84, 240, 32, focus == 1 ? 8 : 2, UI_DELETE, "Delete ROM");
        symbolButton(8, 128, 240, 28, focus == 2 ? 8 : 2, UI_BACK, "Back");
        drawNavigation(1);
        int key = waitDashboardKey();
        if (dashboardNavigation(1, key) || key & KEY_B) return;
        if (key & KEY_UP) { focus = (focus + 2) % 3; continue; }
        if (key & KEY_DOWN) { focus = (focus + 1) % 3; continue; }
        if (key & KEY_A && !(key & KEY_TOUCH)) {
            if (focus == 2) return;
            key = (key & ~KEY_A) | (focus == 0 ? KEY_A : KEY_X);
        }
        if (key & KEY_TOUCH) {
            if (lastTouch.py >= 128 && lastTouch.py < 156) return;
            if (lastTouch.py >= 84 && lastTouch.py < 116) key |= KEY_X;
            else if (lastTouch.py >= 40 && lastTouch.py < 72) key |= KEY_A;
        }
        if (key & KEY_A) showNativeSaves(game);
        else if (key & KEY_X && confirmAction("Delete ROM", "Delete this installed game from SD?\nSave files and server backups are preserved.")) {
            if (!libraryLocalDelete(index)) message("ROM deletion failed", lastError);
            else if (clientRegistered()) libraryRefresh();
            return;
        }
    }
}

static void showSdCard(void) {
    static bool scanned;
    unsigned choice = 0;
    if (!scanned) {
        if (!libraryLocalScan(appPath)) { message("SD library", lastError); return; }
        scanned = true;
    }
    bool resolve = true;
    while (!quitting && !navigationRequest) {
        unsigned amount = libraryLocalCount();
        if (amount && choice >= amount) choice = amount - 1;
        if (amount && resolve) { selectSdDetail(libraryLocalGame(choice)); resolve = false; }
        if (amount) coverPage(&sdDetail, *sdDetail.id ? 1 : 0, 0);
        char status[96];
        struct statvfs space;
        if (!statvfs("/", &space)) {
            uint64_t block = space.f_frsize ? space.f_frsize : space.f_bsize;
            snprintf(status, sizeof(status), "%u games / %llu MiB free", amount, (unsigned long long)((uint64_t)space.f_bavail * block / (1024 * 1024)));
        } else snprintf(status, sizeof(status), "%u installed games", amount);
        if (amount) drawFocusedGame(&sdDetail, status);
        else {
            drawTopHeading("SD card"); uiSymbol(true, 120, 64, UI_SD, 4);
            uiText(true, "No installed games found", 16, 100, 224, 2);
            uiText(true, status, 16, 156, 224, 2);
        }
        frame(&bottom, "SD card");
        uiButton(176, 0, 32, 26, 2); uiSymbol(false, 184, 5, UI_SAVE, 6);
        uiButton(216, 0, 32, 26, 2); uiSymbol(false, 224, 6, UI_TRANSFER, 6);
        unsigned first = choice / 6 * 6;
        for (unsigned index = first; index < amount && index < first + 6; index++) {
            int y = 30 + (index - first) * 22;
            uiListRow(y, libraryLocalGame(index)->title, index + 1, index == choice);
        }
        if (!amount) uiText(false, "Your SD library is empty", 16, 68, 224, 2);
        drawNavigation(1);
        int key = waitDashboardKey();
        if (dashboardNavigation(1, key) || key & KEY_B) return;
        if (key & KEY_TOUCH) {
            if (lastTouch.py < 28) key |= lastTouch.px >= 216 ? KEY_X : KEY_Y;
            else if (lastTouch.py < 162) {
                unsigned index = first + (lastTouch.py - 30) / 22;
                if (index < amount) { if (index == choice) key |= KEY_A; choice = index; resolve = true; }
            }
        }
        if (amount && key & KEY_UP) { choice = (choice + amount - 1) % amount; resolve = true; }
        else if (amount && key & KEY_DOWN) { choice = (choice + 1) % amount; resolve = true; }
        else if (key & KEY_LEFT && first) { choice = first - 6; resolve = true; }
        else if (key & KEY_RIGHT && first + 6 < amount) { choice = first + 6; resolve = true; }
        else if (amount && key & KEY_A) { sdGameActions(choice); resolve = true; }
        else if (key & KEY_X) {
            if (!libraryLocalScan(appPath)) message("SD refresh", lastError);
            else if (clientRegistered()) { libraryRefresh(); remoteLibrary(); }
            resolve = true;
        } else if (key & KEY_Y && confirmAction("Back up all saves", "Back up all nonempty .sav files on this SD card to your server?"))
            runSaveRequest("backup", NULL, NULL, NULL, false);
    }
}

static void drawQueue(unsigned selectedItem) {
        unsigned coverFirst = selectedItem / PAGE_SIZE * PAGE_SIZE;
        coverPage(downloads.items + coverFirst, downloads.count - coverFirst, selectedItem - coverFirst);
    if (downloads.count) {
        char status[96];
        snprintf(status, sizeof(status), "Queue %u / %s", downloads.count, systemLabel(downloads.items[selectedItem].system));
        drawFocusedGame(&downloads.items[selectedItem], status);
    } else {
        drawTopHeading("Transfers"); uiSymbol(true, 120, 56, UI_TRANSFER, 4);
        uiText(true, "Your queue is empty", 16, 88, 224, 1);
    }
    frame(&bottom, "Transfers");
    uiButton(128, 0, 64, 26, downloads.count ? 8 : 2); uiText(false, "Start", 140, 5, 44, 1);
    uiButton(192, 0, 56, 26, 2); uiText(false, "Remove", 194, 5, 54, 1);
    unsigned first = selectedItem < 6 ? 0 : selectedItem - 5;
    for (unsigned i = first; i < downloads.count && i < first + 6; i++) {
        int y = 30 + (i - first) * 22;
        uiListRow(y, downloads.items[i].title, i + 1, i == selectedItem);
    }
    if (!downloads.count) uiText(false, "No pending downloads", 16, 68, 224, 2);
    drawNavigation(2);
    uiPresent();
}

static int waitQueueKey(unsigned selectedItem) {
    while (pmMainLoop()) {
        swiWaitForVBlank(); scanKeys(); uiAnimate();
        clientTick(true);
        if (clientPending()) return KEY_B;
        int pending = deferredKeys; deferredKeys = 0;
        int key = keysDown() | (keysDownRepeat() & (KEY_UP | KEY_DOWN)) | pending;
        if (key & KEY_TOUCH) {
            if (!(pending & KEY_TOUCH)) touchRead(&lastTouch);
            if (!uiTouchFeedback(lastTouch.px, lastTouch.py)) key &= ~KEY_TOUCH;
        }
        if (key) return key;
        if (coverPrefetchTick(selectedItem % PAGE_SIZE)) drawQueue(selectedItem);
    }
    quitting = true;
    return KEY_B;
}

static bool queuedTransfer(const Game *game, void *context) {
    drawQueue(0);
    bool ok = prepareAndTransfer(game, context);
    sdSpaceDue = 0;
    return ok;
}

static void showQueue(void) {
    unsigned selectedItem = 0;
    while (!quitting) {
        drawQueue(selectedItem);
        unsigned previousSelected = selectedItem;
        int key = waitQueueKey(selectedItem);
        if (key & KEY_TOUCH) {
            touchPosition touch = lastTouch;
            if (touch.py >= 164) {
                if (touch.px < 128 || touch.px >= 192) { navigationRequest = navigationAt(touch.px); break; }
                continue;
            }
            if (touch.py < 28) key |= touch.px >= 192 ? KEY_X : KEY_A;
            else if (touch.py < 162) {
                unsigned target = (selectedItem < 6 ? 0 : selectedItem - 5) + (touch.py - 30) / 22;
                if (target < downloads.count) selectedItem = target;
            }
        }
        if ((key & KEY_START) && (keysHeld() & KEY_L)) { quitting = true; break; }
        if (key & KEY_SELECT) {
            navigationRequest = selectNavigation(2);
            if (navigationRequest) break;
            continue;
        }
        if (key & KEY_B) break;
        if ((key & KEY_UP) && downloads.count) selectedItem = (selectedItem + downloads.count - 1) % downloads.count;
        else if ((key & KEY_DOWN) && downloads.count) selectedItem = (selectedItem + 1) % downloads.count;
        else if ((key & KEY_X) && downloads.count) {
            Game removed = downloads.items[selectedItem];
            queueRemove(&downloads, selectedItem);
            if (!saveDownloadQueue(&downloads, NULL)) {
                memmove(&downloads.items[selectedItem + 1], &downloads.items[selectedItem], (downloads.count - selectedItem) * sizeof(Game));
                downloads.items[selectedItem] = removed;
                downloads.count++;
                message("Queue not saved", lastError);
            }
            if (selectedItem && selectedItem >= downloads.count) selectedItem--;
        } else if ((key & (KEY_A | KEY_START)) && downloads.count) {
            if (!saveDownloadQueue(&downloads, NULL)) { message("Queue not saved", lastError); continue; }
            queueRunning = true; queueBatchSize = downloads.count;
            bool ok = queueRun(&downloads, queuedTransfer, saveDownloadQueue, NULL, &queueCompleted);
            queueRunning = false; selectedItem = 0;
            if (quitting) break;
            if (ok) message("Queue complete", "All games are on your SD card. Open /roms/nds/ or /roms/gba/ in TWiLight Menu++.");
            else message("Queue paused / stopped", lastError);
        }
        if (selectedItem != previousSelected && (key & (KEY_UP | KEY_DOWN | KEY_TOUCH)))
            uiCoverTransition(key & KEY_UP ? -1 : key & KEY_DOWN ? 1 : selectedItem > previousSelected ? 1 : -1);
    }
}

static void previewTheme(const Game *game) {
    char path[128], magic[8];
    u16 *pixels = malloc(256 * 384 * 2);
    if (!pixels) { message("Theme preview", "Not enough memory for preview"); return; }
    progress(game->title, "Loading dual-screen preview...", 0, 0);
    snprintf(path, sizeof(path), "/api/themes/preview?id=%s&native=1", game->id);
    Http *http = &downloadHttp;
    bool ok = httpOpen(http, "GET", path, 0);
    unsigned received = 0;
    if (ok) {
        ok = http->status == 200 && http->length == 8 + 256 * 384 * 2;
        unsigned header = 0;
        while (ok && header < sizeof(magic)) {
            int amount = httpRead(http, magic + header, sizeof(magic) - header);
            if (amount <= 0) ok = false; else header += amount;
        }
        ok = ok && !memcmp(magic, "RFTMPRE1", 8);
        while (ok && received < 256 * 384 * 2) {
            unsigned want = 256 * 384 * 2 - received;
            if (want > 8192) want = 8192;
            int amount = httpRead(http, (unsigned char *)pixels + received, want);
            if (amount <= 0 || !appYield()) ok = false; else received += amount;
        }
        httpClose(http);
    }
    if (ok) {
        DC_FlushRange(pixels, 256 * 384 * 2);
        ok = uiThemePreview(pixels);
        if (ok) { waitKey(); uiThemePreviewEnd(); }
    }
    free(pixels);
    if (!ok && !quitting) message("Theme preview", "No preview available from the source, or connection interrupted.");
}

static void themeSelected(Game *game) {
    bool favorite = strchr(game->languages, '*') != NULL;
    unsigned focus = 2;
    while (!quitting) {
        frame(&bottom, "Theme");
        uiText(false, game->title, 16, 32, 224, 3);
        uiCaption(false, game->region, 16, 80, 224, 6);
        symbolButton(8, 96, 240, 24, focus == 0 ? 8 : 2, UI_PREVIEW, "Dual-screen preview");
        symbolButton(8, 120, 240, 24, focus == 1 ? 8 : 2, UI_FAVORITE, favorite ? "Remove favorite" : "Add favorite");
        symbolButton(8, 144, 240, 24, focus == 2 ? 4 : 2, UI_DOWNLOAD, "Install theme");
        symbolButton(8, 168, 240, 24, focus == 3 ? 8 : 2, UI_BACK, "Back");
        int key = waitKey();
        if (key & KEY_UP) { focus = (focus + 3) % 4; continue; }
        if (key & KEY_DOWN) { focus = (focus + 1) % 4; continue; }
        if (key & KEY_A && !(key & KEY_TOUCH)) {
            static const int actions[] = {KEY_Y, KEY_X, KEY_A, KEY_B};
            key = (key & ~KEY_A) | actions[focus];
        }
        if (key & KEY_TOUCH) {
            if (lastTouch.py >= 168) key |= KEY_B;
            else if (lastTouch.py >= 144) key |= KEY_A;
            else if (lastTouch.py >= 120) key |= KEY_X;
            else if (lastTouch.py >= 96) key |= KEY_Y;
        }
        if (key & KEY_B) return;
        if (key & KEY_Y) previewTheme(game);
        else if (key & KEY_X) {
            char path[128];
            snprintf(path, sizeof(path), "/api/themes/favorite?id=%s", game->id);
            if (httpText("POST", path, response, sizeof(response))) {
                favorite = !favorite;
                strcpy(game->languages, favorite ? "Themes*" : "Themes");
            } else message("Favorite", lastError);
        } else if (key & KEY_A) {
            if (!prepareAndTransfer(game, NULL) && !quitting) message("Theme installation", lastError);
            return;
        }
    }
}

static void downloadSelected(void) {
    if (!count) return;
    readSdSpace(true);
    drawFocusedGame(&games[selected], NULL);
    if (games[selected].system == SYSTEM_OTHERS && !strncmp(games[selected].languages, "Themes", 6)) {
        themeSelected(&games[selected]); return;
    }
    const Game game = games[selected];
    frame(&bottom, "Download");
    uiText(false, game.title, 16, 34, 224, 3);
    uiCaption(false, game.region, 16, 80, 224, 6);
    char amount[24], storageLabel[64];
    if (game.size) formatStorageSize(game.size, amount, sizeof(amount)); else strcpy(amount, "Unknown");
    snprintf(storageLabel, sizeof(storageLabel), "Game size: %s", amount);
    uiSymbol(false, 16, 98, UI_INFO, 6);
    uiText(false, storageLabel, 40, 98, 200, 1);
    if (sdSpaceKnown) formatStorageSize(availableSdBytes, amount, sizeof(amount)); else strcpy(amount, "Unavailable");
    snprintf(storageLabel, sizeof(storageLabel), "SD free: %s", amount);
    uiSymbol(false, 16, 118, UI_SD, 6);
    uiText(false, storageLabel, 40, 118, 200, 1);
    symbolButton(8, 144, 240, 24, 4, UI_DOWNLOAD, "Download / resume");
    symbolButton(8, 168, 156, 24, 2, UI_ADD, "Add to queue");
    symbolButton(168, 168, 80, 24, 2, UI_BACK, "Back");
    int key;
    do {
        key = waitKey();
        if (key & KEY_TOUCH) {
            touchPosition touch = lastTouch;
            if (touch.py >= 144 && touch.py < 168) key |= KEY_A;
            else if (touch.py >= 168) key |= touch.px < 164 ? KEY_X : KEY_B;
        }
    } while (!(key & (KEY_A | KEY_B | KEY_X)));
    if (key & KEY_B) return;
    if (key & KEY_X) { enqueueGame(&game); return; }
    bool ok = prepareAndTransfer(&game, NULL);
    sdSpaceDue = 0;
    if (!ok && !quitting) message("Download", lastError);
}

int main(int argc, char **argv) {
    setBrightness(3, -16);
    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
#ifdef ROMSFINDER_DIRTY_VRAM_TEST

    dmaFillHalfWords(0x0111, BG_GFX_SUB, 128 * 1024);
#endif
    consoleInit(&top, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);
    // Sub VRAM: keyboard tiles [0,40 KiB), keyboard map [40,44 KiB),
    // console map [44,46 KiB), console font [48,56 KiB). Never share tile base 0.
    consoleInit(&bottom, 0, BgType_Text4bpp, BgSize_T_256x256, 22, 3, false, true);
    Keyboard *keyboard = keyboardInit(NULL, 3, BgType_Text4bpp, BgSize_T_256x512, 20, 0, false, true);
    clearKeyboardBackground(keyboard);
    bgSetPriority(bottom.bgId, 1);
    bgSetPriority(keyboard->background, 0);
    keyboard->scrollSpeed = 0;
    keyboardHide();
    uiInit(&top, &bottom, keyboard);
    keysSetRepeat(25, 6);
    srand((unsigned)time(NULL));
#if defined(ROMSFINDER_INTRO_TEST) || !(defined(ROMSFINDER_UI_TEST) || defined(ROMSFINDER_STARTUP_TEST) || defined(ROMSFINDER_SEARCH_TEST) || defined(ROMSFINDER_PROTOCOL_TEST) || defined(ROMSFINDER_QUEUE_TEST) || defined(ROMSFINDER_DISPLAY_TEST) || defined(ROMSFINDER_CLIENT_TEST) || defined(ROMSFINDER_COVER_TEST))
    if (!playIntro()) return 0;
#else
    setBrightness(3, 0);
#endif
    drawBrand();
    startup(0, "Checking your SD card...");
#ifdef ROMSFINDER_CLIENT_TEST
    int clientResult = runClientTests();
    if (!clientResult) clientResult = runClientNetworkTests();
    message("Client tests passed", "Three systems, durable remote jobs, MAC/server binding and completion ACK checks passed on ARM9.");
    return clientResult;
#endif
#ifdef ROMSFINDER_COVER_TEST
    int coverResult = runCoverTests();
    message("Cover tests passed", "All ten page covers, selected first, nonblocking IO, stale cancellation and cache scope passed on ARM9.");
    return coverResult;
#endif
#if defined(ROMSFINDER_INTRO_TEST) && !defined(ROMSFINDER_UI_TEST)
    while (pmMainLoop()) { appYield(); }
    return 0;
#endif
#ifdef ROMSFINDER_STARTUP_TEST
    waitKey(); startup(1, "Finding your Wi-Fi...");
    waitKey(); startup(1, "Authenticating WPA2...");
    waitKey(); startup(2, "Loading your catalog...");
    waitKey(); message("Ready to browse", "Your library is connected. Pick a game to get started.");
    return 0;
#endif
#ifdef ROMSFINDER_SEARCH_TEST
    char searchExample[81] = "The Legend of the Lost Island: A New Adventure";
    inputText("SEARCH LIBRARY", searchExample, sizeof(searchExample));
    message("Search ready", searchExample);
    return 0;
#endif
#ifdef ROMSFINDER_PROTOCOL_TEST
    int result = runProtocolTests(1, NULL);
    message("Protocol tests passed", "NDS/GBA/Others IDs, upload filenames, empty catalogs, bounds and transfer CRC32 passed on ARM9.");
    return result;
#endif
#ifdef ROMSFINDER_QUEUE_TEST
    int queueResult = runQueueTests();
    message("Queue tests passed", "Mixed NDS/GBA/Others order, persistence, pause/retry, scoped duplicates and truncated saves passed on ARM9.");
    return queueResult;
#endif
#ifdef ROMSFINDER_DISPLAY_TEST

    char testSsid[33] = "Home Network";
    char testPassword[65] = "DemoKey1234";
    inputText("WI-FI NETWORK / SSID", testSsid, sizeof(testSsid));
    inputField("WI-FI PASSWORD / WPA2", testPassword, sizeof(testPassword), true);
    message("Settings saved", "Your Wi-Fi network is ready. Text and keyboard stay readable on both displays.");
    return 0;
#endif
#ifdef ROMSFINDER_UI_TEST
    count = 10; total = 1248; page = 2; pages = 125; selected = 2;
    for (int i = 0; i < count; i++) {
        snprintf(games[i].title, sizeof(games[i].title), "Example Adventure %d", i + 1);
        strcpy(games[i].region, "Europe"); strcpy(games[i].languages, "En,Fr,De");
        games[i].size = 32U * 1024 * 1024;
        snprintf(games[i].id, sizeof(games[i].id), "%032x", i + 1);
        games[i].system = SYSTEM_NDS;
    }
    strcpy(games[2].title, "The Legend of the Lost Island: A New Adventure");
    coverTestFixture(&games[2]);
    strcpy(settings.ssid, "Home Network");
    queueAdd(&downloads, &games[2]);
    Game exampleGba = games[1];
    exampleGba.system = SYSTEM_GBA; strcpy(exampleGba.id, "cbf43926");
    strcpy(exampleGba.title, "GBA Homebrew Adventure");
    queueAdd(&downloads, &exampleGba);
    coverTestFixture(&exampleGba);
    Game exampleOther = games[2]; exampleOther.system = SYSTEM_OTHERS;
    strcpy(exampleOther.title, "My Uploaded Homebrew");
    queueAdd(&downloads, &exampleOther);
    coverTestMissingFixture(&exampleOther);
#ifdef ROMSFINDER_MOTION_TEST
    for (int i = 0; i < count; i++) coverTestFixture(&games[i]);
    bool motionRedraw = true;
    while (pmMainLoop()) {
        if (motionRedraw) { drawBrowser(); motionRedraw = false; }
        swiWaitForVBlank(); scanKeys(); uiAnimate();
        int key = catalogInput(0);
        if (key & KEY_START) break;
        if (key & (KEY_UP | KEY_DOWN)) {
            int direction = key & KEY_UP ? -1 : 1;
            selected = (selected + count + direction) % count;
            uiCoverTransition(direction); motionRedraw = true;
        }
        /* Exercise status redraws while sprites are still moving. */
        if (key & KEY_X) motionRedraw = true;
    }
    message("Cover motion passed", "Cover carousel, rapid reversal and unchanged-cover redraws completed.");
    return 0;
#endif
#ifdef ROMSFINDER_SYSTEM_TEST
    assert(activeSystem == SYSTEM_NDS);
    assert(visibleSystem(0) == SYSTEM_NDS && visibleSystem(1) == SYSTEM_GBA);
    assert(systemTouchAction(32) == ACTION_NDS && systemTouchAction(160) == ACTION_GBA);
    assert(requestedSystem(KEY_L) == SYSTEM_OTHERS && requestedSystem(KEY_R) == SYSTEM_GBA);
    activeSystem = SYSTEM_GBA;
    assert(visibleSystem(0) == SYSTEM_GBA && visibleSystem(1) == SYSTEM_OTHERS);
    assert(systemTouchAction(32) == ACTION_GBA && systemTouchAction(160) == ACTION_OTHERS);
    assert(requestedSystem(KEY_L) == SYSTEM_NDS && requestedSystem(KEY_R) == SYSTEM_OTHERS);
    activeSystem = SYSTEM_OTHERS;
    assert(visibleSystem(0) == SYSTEM_GBA && visibleSystem(1) == SYSTEM_OTHERS);
    assert(requestedSystem(systemTouchAction(32)) == SYSTEM_GBA);
    assert(requestedSystem(systemTouchAction(160)) == SYSTEM_OTHERS);
    assert(requestedSystem(KEY_L) == SYSTEM_GBA && requestedSystem(KEY_R) == SYSTEM_NDS);
    assert(requestedSystem(KEY_Y) == SYSTEM_NDS);
    activeSystem = SYSTEM_NDS;
    bool systemRedraw = true;
    while (pmMainLoop()) {
        if (systemRedraw) { drawBrowser(); systemRedraw = false; }
        swiWaitForVBlank(); scanKeys();
        int key = catalogInput(0);
        if (key & KEY_START) break;
        if (key & KEY_TOUCH) {
            touchPosition touch; touchRead(&touch);
            if (uiTouchFeedback(touch.px, touch.py) && touch.py < 24) key |= systemTouchAction(touch.px);
        }
        if (key & (KEY_L | KEY_R | KEY_Y | ACTION_SYSTEMS)) {
            activeSystem = requestedSystem(key);
            for (int i = 0; i < count; i++) {
                games[i].system = activeSystem;
                snprintf(games[i].id, sizeof(games[i].id), activeSystem == SYSTEM_GBA ? "%08x" : "%032x", i + 1);
            }
            strcpy(games[0].title, activeSystem == SYSTEM_OTHERS ? "My Uploaded Homebrew" : "Example Adventure 1");
            strcpy(games[selected].title, activeSystem == SYSTEM_OTHERS ? "My Uploaded Homebrew" :
                activeSystem == SYSTEM_GBA ? "GBA Homebrew Adventure" : "The Legend of the Lost Island: A New Adventure");
            systemRedraw = true;
        } else if (key & KEY_RIGHT) { page++; systemRedraw = true; }
        else if (key & KEY_LEFT) { page--; systemRedraw = true; }
    }
    message("Controls passed", "NDS/GBA/Others, sliding touch tabs, L/R once per press and LEFT/RIGHT pages passed.");
    return 0;
#endif
#ifdef ROMSFINDER_QUEUE_PREVIEW_TEST
    drawBrowser(); waitKey();
    showQueue();
    message("Queue preview passed", "All three libraries keep their own cover, title and SD destination.");
    return 0;
#endif
    drawBrowser(); waitKey();
#ifdef ROMSFINDER_REGION_TEST
    chooseRegion(); drawBrowser(); waitKey();
    return 0;
#endif
#ifdef ROMSFINDER_FEEDBACK_TEST
    uiTouchFeedback(140, 176);
#endif
    selected = 8;

    for (unsigned redrawTest = 0; redrawTest < 30; redrawTest++) drawBrowser();
    waitKey();
    activeSystem = SYSTEM_GBA; selected = 1; total = 659; pages = 66; page = 0;
    for (int i = 0; i < count; i++) {
        games[i].system = SYSTEM_GBA;
        snprintf(games[i].id, sizeof(games[i].id), "%08x", i + 1);
    }
    strcpy(games[1].title, "GBA Homebrew Adventure");
    drawBrowser(); waitKey();
    drawQueue(1); waitKey();
    drawSettings(); waitKey();
    queueRunning = true; queueBatchSize = 2; queueCompleted = 1; transferSystem = SYSTEM_GBA;
    queueRemove(&downloads, 0); drawQueue(0);
    progress(exampleGba.title, "SD download: 128 KiB/s", 6U * 1024 * 1024, 16U * 1024 * 1024); waitKey();
    queueRunning = false;
    message("Queue complete", "All games are saved in their own SD folders. Return to TWiLight Menu++ to play.");
    return 0;
#endif
    if (!isDSiMode()) {
        message("DSi mode required", "In TWiLight Menu++, press Y on this app and set Run in to DSi mode. Launch from the internal DSi SD card.");
        return 0;
    }
    if (!fatInitDefault()) {
        message("SD card unavailable", "Launch from the internal DSi SD card using TWiLight Menu++. Check write protection and FAT32 formatting.");
        return 0;
    }

    findAppPath(argc, argv);
    if (chdir("sd:/") != 0 && chdir("fat:/") != 0) {
        message("SD card", "No writable SD drive found."); return 0;
    }
    loadSettings();
    loadDownloadQueue();
    bool ready = wifiSettingsValid(&settings, lastError, sizeof(lastError));
    if (!ready && !quitting) ready = configureWifi();
    if (ready && !quitting) {
        bool connected = connectWifi(true);
        if (connected && !fetchPage(0)) connected = false;
        if (!connected && !quitting) message("Connection", lastError);
    }
    bool redraw = true;
    while (!quitting && pmMainLoop()) {
        if (redraw) { drawBrowser(); redraw = false; }
        swiWaitForVBlank(); scanKeys(); uiAnimate();
        int previousSelected = selected;
        int pending = deferredKeys; deferredKeys = 0;
        int key = catalogInput(pending);
        if (key & KEY_TOUCH) {
            touchPosition touch;
            if (pending & KEY_TOUCH) touch = lastTouch; else touchRead(&touch);
            if (!uiTouchFeedback(touch.px, touch.py)) continue;
            if (touch.py < 28) key |= touch.px < 136 ? (unsigned)systemTouchAction(touch.px) :
                touch.px < 172 ? ACTION_RANDOM : touch.px < 208 ? KEY_X : ACTION_REGION;
            else if (touch.py < 162) {
                int target = (selected < 6 ? 0 : selected - 5) + (touch.py - 30) / 22;
                if (target >= 0 && target < count) {
                    key |= target == selected ? KEY_A : ACTION_SELECT;
                    selected = target;
                }
            } else if (touch.py >= 164) key |= touch.px < 64 ? ACTION_SELECT :
                touch.px < 128 ? ACTION_SD : touch.px < 192 ? KEY_START : ACTION_SETUP;
        }
        unsigned revision = clientRevision();
        clientTick(!key);
        if (revision != clientRevision()) redraw = true;
        if (!key && remoteTransfer()) { redraw = true; continue; }
        if (!key && remoteLibrary()) { redraw = true; continue; }
        if (!key && remoteSaves()) { redraw = true; continue; }
        if (!key) {
            if (coverPrefetchTick(selected)) redraw = true;
            continue;
        }
        if (key & (KEY_LEFT | KEY_RIGHT | KEY_SELECT | KEY_X | KEY_A | KEY_START | KEY_B | ACTION_REGION | KEY_L | KEY_R | KEY_Y | ACTION_SYSTEMS | ACTION_RANDOM)) coverPausePrefetch();
        if ((key & KEY_START) && (keysHeld() & KEY_L)) break;
        redraw = true;
        browserNotice[0] = 0;
        if ((key & KEY_UP) && count) selected = (selected + count - 1) % count;
        else if ((key & KEY_DOWN) && count) selected = (selected + 1) % count;
        else if ((key & KEY_LEFT) && page > 0) {
            if (!fetchPage(page - 1)) message("Library", lastError);
        } else if ((key & KEY_RIGHT) && page + 1 < pages) {
            if (!fetchPage(page + 1)) message("Library", lastError);
        } else if (key & ACTION_SD) {
            showSdCard();
        } else if (key & KEY_SELECT) {
            navigationRequest = selectNavigation(0);
        } else if (key & ACTION_SETUP) {
            ConfigChange change = configure();
            if (change == CONFIG_UPDATE && !quitting) {
                if (connectWifi(false)) updateApp();
                else message("Update connection", lastError);
            } else if (change != CONFIG_NONE && !quitting && ((change != CONFIG_FILTER && !connectWifi(change == CONFIG_WIFI)) || !fetchPage(0)))
                message("Connection", lastError);
        } else if (key & KEY_X) {
            if (inputText("SEARCH LIBRARY", query, sizeof(query)) && !fetchPage(0)) message("Search", lastError);
        } else if (key & (ACTION_REGION | KEY_B)) {
            if (chooseRegion()) {
                drawBrowser();
                if (!fetchPage(0)) message(activeSystem == SYSTEM_OTHERS ? "Category" : "Region", lastError);
            }
        } else if (key & (KEY_Y | ACTION_RANDOM)) {
            if (!fetchRandomGame()) message("Random game", lastError);
        } else if (key & (KEY_L | KEY_R | ACTION_SYSTEMS)) {
            RomSystem before = activeSystem;
            RomSystem next = requestedSystem(key);
            if (next != before) {
                char previousQuery[sizeof(query)]; memcpy(previousQuery, query, sizeof(query));
                int previousRegion = regionIndex;
                activeSystem = next; query[0] = 0; regionIndex = 0;
                if (!fetchPage(0)) {
                    activeSystem = before; regionIndex = previousRegion;
                    memcpy(query, previousQuery, sizeof(query));
                    message("Switch system", lastError);
                }
            }
        } else if ((key & ACTION_ADD_QUEUE) && count) enqueueGame(&games[selected]);
        else if (key & KEY_A) downloadSelected();
        else if (key & KEY_START) {
            showQueue();
        }
        if (selected != previousSelected && (key & (KEY_UP | KEY_DOWN | KEY_TOUCH)))
            uiCoverTransition(key & KEY_UP ? -1 : key & KEY_DOWN ? 1 : selected > previousSelected ? 1 : -1);
        if (navigationRequest) { deferredKeys |= navigationRequest; navigationRequest = 0; }
        if (!(key & (KEY_UP | KEY_DOWN))) coverPage(games, count, selected);
    }
    clientClose(); coverPausePrefetch(); libraryLocalFree();
    if (wifiInitialized) wlmgrStop();
    return 0;
}
