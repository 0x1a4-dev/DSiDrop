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
static PrintConsole *activeScreen;
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
static char browserNotice[48];
static bool browserVisible, loadingCover;
static unsigned coverFrames;
static int deferredKeys;
static touchPosition lastTouch;
static bool startupVisible, settingsVisible;
static void drawSettings(void);
static unsigned startupStage, startupTick;
static char startupDetail[96];
#define ACTION_ADD_QUEUE (1 << 20)
#define ACTION_REGION (1 << 21)
#define ACTION_NDS (1 << 22)
#define ACTION_GBA (1 << 23)
#define ACTION_OTHERS (1 << 24)
#define ACTION_RANDOM (1 << 25)
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
    return actions[visibleSystem(x < 102 ? 0 : 1)];
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

static void position(PrintConsole *screen, int row, int column) {
    consoleSelect(screen);
    activeScreen = screen;
    uiInvalidate(screen == &top);
    // libnds stores console coordinates as zero-based (unlike ANSI terminals).
    screen->cursorY = row - 1;
    screen->cursorX = column - 1;
}

static void clear(PrintConsole *screen) {
    if (screen == &bottom) { browserVisible = false; startupVisible = false; settingsVisible = false; }
    consoleSelect(screen); activeScreen = screen; consoleClear();
    uiClear(screen == &top);
    iprintf("\x1b[39m");
}

static void frame(PrintConsole *screen, const char *title) {
    clear(screen);
    uiPanel(screen == &top, 4, 0, 248, 24, 2);
    position(screen, 2, 3); iprintf("\x1b[36m%.28s\x1b[39m", title);
}

static void wrapped(const char *text, int maxLines) {
    PrintConsole *screen = activeScreen;
    int column = screen->cursorX;
    if (column < 1) column = 1;
    int row = screen->cursorY;
    unsigned width = 30 - column;
    while (*text && maxLines--) {
        unsigned n = strlen(text);
        if (n > width) n = width;
        const char *newline = memchr(text, '\n', n);
        if (newline) n = newline - text;
        else if (strlen(text) > n && text[n] != ' ') {
            unsigned word = n;
            while (word && text[word] != ' ') word--;
            if (word) n = word;
        }
        position(screen, ++row, column + 1); iprintf("%.*s", (int)n, text);
        text += n;
        if (*text == '\n') text++;
        while (*text == ' ') text++;
    }
}

static void drawBrand(void) {
    clear(&top); uiBrand(true);
    position(&top, 18, 4); iprintf("\x1b[37mYour library. On your DSi.");
    position(&top, 22, 3); iprintf("\x1b[36mNDS / GBA / Others");
    position(&top, 22, 25); iprintf("\x1b[37m" APP_VERSION);
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
    uiText(false, "Getting ready", 16, 6, 224, 1);
    position(&bottom, 4, 3); iprintf("\x1b[37mYour library is on its way.");
    static const char *labels[] = {"SD card", "Wi-Fi network", "PC library"};
    for (unsigned i = 0; i < 3; i++) {
        unsigned state = i < startupStage ? 2 : i == startupStage ? 1 : 0;
        int y = 44 + i * 38;
        uiPanel(false, 8, y, 240, 34, state == 1 ? 8 : 2);
        uiStageMark(16, y + 8, state);
        uiText(false, labels[i], 42, y, 196, 1);
        position(&bottom, (y + 20) / 8 + 1, 6);
        iprintf("\x1b[37m%.24s", state == 2 ? "Ready" : state == 1 ? startupDetail : "Waiting");
    }
    uiBusy(startupTick * 3);
    uiButton(8, 168, 240, 24, 2);
    position(&bottom, 23, 3); iprintf("\x1b[36m[B]\x1b[39m Cancel connection");
    startupVisible = true;
    uiPresent();
}

static void startup(unsigned stage, const char *detail) {
    startupStage = stage;
    snprintf(startupDetail, sizeof(startupDetail), "%s", detail);
    drawStartup();
}

static bool loadCoverPreview(const Game *game) {
    char savedError[sizeof(lastError)]; memcpy(savedError, lastError, sizeof(lastError));
    loadingCover = true; coverFrames = 0;
    bool complete = coverLoad(game);
    loadingCover = false;
    memcpy(lastError, savedError, sizeof(lastError));
    return complete;
}

static int waitKey(void) {
    uiPresent();
    while (pmMainLoop()) {
        if (startupVisible && ++startupTick % 10 == 0) drawStartup();
        swiWaitForVBlank(); scanKeys();
        unsigned revision = clientRevision();
        clientTick(false);
        if (settingsVisible && revision != clientRevision()) drawSettings();
        int key = keysDown();
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
    if (startupVisible && !loadingCover && ++startupTick % 10 == 0) drawStartup();
    uiPresent();
    if (wait) swiWaitForVBlank();
    scanKeys();
    lastPoll = tickGetCount();
    clientTick(false);
    if (loadingCover) {
        int key = catalogInput(0);
        if (key & KEY_TOUCH) touchRead(&lastTouch);
        deferredKeys |= key;

        return !key && ++coverFrames < 60 * 5;
    }
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
    if (queueRunning) snprintf(heading, sizeof(heading), "QUEUE %u/%u / %s", queueCompleted + 1, queueBatchSize,
        systemLabel(transferSystem));
    else snprintf(heading, sizeof(heading), "%s", clientActive() ? "FROM WEB / DOWNLOAD TO SD" : size ? "TRANSFER" : "CONNECTING / PREPARING");
    frame(&bottom, heading);
    position(&bottom, 5, 3);
    wrapped(title, 3);
    position(&bottom, 10, 3); iprintf("\x1b[37m"); wrapped(message, 3);
    if (size) {
        unsigned percent = (unsigned)((uint64_t)done * 100 / size);
        if (percent > 100) percent = 100;
        uiMeter(percent);
        position(&bottom, 20, 3); iprintf("\x1b[39m%lu / %lu KiB", (unsigned long)(done / 1024), (unsigned long)(size / 1024));
    } else {
        static unsigned phase;
        position(&bottom, 17, 3); iprintf("\x1b[36mWorking%.*s", (int)(++phase % 4), "...");
        position(&bottom, 20, 3); iprintf("\x1b[37mKeep your DSi open.");
    }
    uiButton(8, 168, 240, 24, 2);
    position(&bottom, 23, 3); iprintf("\x1b[36m[B]\x1b[39m Pause / go back");
    uiPresent();
}

static void message(const char *title, const char *text) {
    frame(&bottom, APP_NAME);
    position(&bottom, 5, 3); iprintf("\x1b[33m"); wrapped(title, 3);
    position(&bottom, 9, 3); iprintf("\x1b[39m"); wrapped(text, 11);
    uiButton(8, 168, 240, 24, 2);
    position(&bottom, 23, 3); iprintf("\x1b[36m[A/B]\x1b[39m Back  \x1b[37mSTART Exit");
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
    position(&bottom, 8, 3);
    iprintf("\x1b[37m%s", secret ? "START Save  B Back  R Show" :
        !strcmp(title, "SEARCH LIBRARY") ? "START Search B Back X Clear" : "START Save  B Back  X Clear");
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
            position(&bottom, 5, 3); iprintf("\x1b[39m                            ");
            position(&bottom, 5, 3);
            size_t visibleLength = strlen(edited);
            const char *visible = edited;
            if (visibleLength > 27) { visible += visibleLength - 27; visibleLength = 27; }
            if (secret && !showing) {
                for (unsigned i = 0; i < visibleLength; i++) iprintf("*");
            } else iprintf("%s", visible);
            uiPanel(false, 16 + visibleLength * 8, 32, 3, 12, 4);
            redraw = false;
            uiPresent();
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
    clear(&top);
    uiIcon(true, 112, 8);
    position(&top, 7, 3); iprintf("\x1b[36m" APP_NAME " / " APP_VERSION);
    position(&top, 10, 3); iprintf("\x1b[39mWeb: \x1b[36m%.23s", clientConnectionStatus());
    position(&top, 12, 3); iprintf("\x1b[37mMAC: %s", clientMac());
    struct in_addr ip = { .s_addr = wifiInitialized ? Wifi_GetIP() : 0 };
    position(&top, 14, 3); iprintf("\x1b[37mDSi: %s", inet_ntoa(ip));
    uiPanel(true, 8, 120, 240, 56, 2);
    position(&top, 16, 3); iprintf("\x1b[39m");
    wrapped(clientConnectionDetail(), 3);
    position(&top, 23, 3); iprintf("\x1b[37m%s: %.20s", activeSystem == SYSTEM_OTHERS ? "Category" : "Region", filterLabel(regionIndex));
    frame(&bottom, "SETTINGS");
    uiButton(168, 0, 80, 24, 2);
    position(&bottom, 2, 23); iprintf("\x1b[36m[B]\x1b[39m Back");
    position(&bottom, 5, 3); iprintf("\x1b[37mNETWORK\x1b[39m  %s", settings.wifiMode == WIFI_WPA2 ? "WPA2 / AES" : "System");
    position(&bottom, 7, 3); iprintf("%.28s", *settings.ssid ? settings.ssid : "No custom network saved");
    position(&bottom, 9, 3); iprintf("\x1b[37mPC\x1b[39m %s:%u", settings.host, settings.port);
    uiButton(8, 88, 240, 24, 8);
    position(&bottom, 13, 3); iprintf("\x1b[36m[A]\x1b[39m Set up WPA2 Wi-Fi");
    uiButton(8, 116, 240, 24, 2);
    position(&bottom, 16, 3); iprintf("\x1b[36m[X]\x1b[39m Change PC address");
    uiButton(8, 144, 136, 24, 2); uiButton(152, 144, 96, 24, 8);
    position(&bottom, 20, 3); iprintf("\x1b[36m[Y]\x1b[39m System WiFi");
    position(&bottom, 20, 21); iprintf("\x1b[36m[L]\x1b[39m Update");
    uiButton(4, 176, 122, 16, 2); uiButton(130, 176, 122, 16, 2);
    position(&bottom, 23, 2); iprintf("\x1b[36m[R]\x1b[39m %s", activeSystem == SYSTEM_OTHERS ? "Category" : "Region");
    position(&bottom, 23, 18); iprintf("\x1b[36m[START]\x1b[39m WiFi");
    settingsVisible = true;
    uiPresent();
}

static ConfigChange configure(void) {
    while (!quitting) {
        drawSettings();
        int key = waitKey();
        if (key & KEY_TOUCH) {
            touchPosition touch = lastTouch;
            if (touch.py < 24) key |= KEY_B;
            else if (touch.py >= 88 && touch.py < 112) key |= KEY_A;
            else if (touch.py >= 116 && touch.py < 140) key |= KEY_X;
            else if (touch.py >= 144 && touch.py < 168) key |= touch.px < 148 ? KEY_Y : KEY_L;
            else if (touch.py >= 176) key |= touch.px < 128 ? KEY_R : KEY_START;
        }
        if ((key & KEY_START) && (keysHeld() & KEY_L)) { quitting = true; return CONFIG_NONE; }
        if (key & (KEY_B | KEY_SELECT)) return CONFIG_NONE;
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
        position(&bottom, 17, 3); iprintf("\x1b[37mLoading page...             ");
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
        position(&bottom, 17, 3); iprintf("\x1b[37mPicking a random game...     ");
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

static void drawGameDetail(const Game *game, const char *notice) {
    bool known;
    const u16 *cover = coverFind(game, &known);
    uiPanel(false, 4, 28, 96, 96, 3);
    uiPanel(false, 6, 30, 92, 92, 2);
    if (cover) uiCover(cover);
    else {
        uiIcon(false, 36, 52);
        position(&bottom, 13, 3); iprintf("\x1b[37m%s", known ? "No cover" : "Preview");
    }
    uiTitle(game->title, 108, 32, 136, 5);
    position(&bottom, 15, 15);
    if (notice && *notice) iprintf("\x1b[36m%.17s", notice);
    else iprintf("\x1b[37m%s / %lu MiB", systemLabel(game->system),
        (unsigned long)(game->size / (1024 * 1024)));
}

static void drawBrowser(void) {
    clear(&top);
    uiIcon(true, 0, 0);
    position(&top, 2, 6); iprintf("\x1b[36m" APP_NAME "\x1b[39m");
    position(&top, 2, 17); iprintf("\x1b[37m%s", clientRegistered() ? "WEB OK" : "WEB...");
    position(&top, 2, 26); iprintf("\x1b[37m%d/%d", count ? selected + 1 : 0, count);
    static const char *libraryNames[] = { "NINTENDO DS", "GAME BOY ADVANCE", "APPS & UPLOADS" };
    position(&top, 3, 6); iprintf("\x1b[37m%s / LIBRARY", libraryNames[activeSystem]);
    int first = selected < 6 ? 0 : selected - 5;
    if (!count) {
        position(&top, 9, 3); iprintf("\x1b[39mNo games found.");
        position(&top, 12, 3); iprintf("\x1b[37m%s", activeSystem == SYSTEM_OTHERS ? "Upload files on your PC." : "Try a different search.");
    }
    for (int i = first; i < count && i < first + 6; i++) {
        int y = 32 + (i - first) * 24;
        if (i == selected) {
            uiPanel(true, 4, y, 248, 23, 8);
            uiPanel(true, 4, y + 2, 3, 19, 4);
        } else if ((i - first) % 2 == 0) uiPanel(true, 4, y, 248, 23, 2);
        position(&top, y / 8 + 1, 3); iprintf("\x1b[39m%.28s", games[i].title);
        position(&top, y / 8 + 2, 3);
        iprintf("\x1b[37m%.15s  %lu MiB", games[i].region, (unsigned long)(games[i].size / (1024 * 1024)));
    }
    position(&top, 23, 2); iprintf("\x1b[36m%d games\x1b[37m  Page %d/%d", total, page + 1, pages);
    position(&top, 24, 2); iprintf("\x1b[37mL/R System  LEFT/RIGHT Page");
    clear(&bottom);
    for (unsigned slot = 0; slot < 2; slot++) {
        RomSystem tab = visibleSystem(slot);
        uiButton(slot ? 104 : 4, 0, 96, 24, activeSystem == tab ? 4 : 2);
        position(&bottom, 2, slot ? 16 : 4);
        iprintf("%s%s", activeSystem == tab ? "\x1b[35m" : "\x1b[39m", systemLabel(tab));
    }
    uiButton(204, 0, 48, 24, 2);
    // Single row matches the tabs' vertical centering; two stacked rows left no bottom margin.
    position(&bottom, 2, 27); iprintf("\x1b[36mY \x1b[37mRND");
    if (count) drawGameDetail(&games[selected], browserNotice);
    else {
        position(&bottom, 6, 3); iprintf("\x1b[39m");
        wrapped(activeSystem == SYSTEM_OTHERS ? "Add your own files in the web app's Others library." : "Search for a game to see its details.", 3);
    }
    uiButton(4, 124, 248, 20, 3);
    uiPanel(false, 6, 126, 244, 16, 2);
    position(&bottom, 17, 3); iprintf("\x1b[37m%s\x1b[39m  %.17s", activeSystem == SYSTEM_OTHERS ? "Category" : "Region", filterLabel(regionIndex));
    uiChevron(236, 132);
    uiButton(4, 144, 122, 23, 2); uiButton(130, 144, 122, 23, 2);
    position(&bottom, 20, 2); iprintf("\x1b[36m[X]\x1b[39m Search");
    position(&bottom, 20, 18); iprintf("\x1b[36m[START]\x1b[39m Q %u", downloads.count);
    uiButton(4, 168, 122, 24, count ? 4 : 3);
    uiButton(130, 168, 122, 24, 8);
    position(&bottom, 23, 2);
    iprintf(count ? "\x1b[35m[A] Download\x1b[39m" : "\x1b[37mSelect game");
    position(&bottom, 23, 18); iprintf("\x1b[36m[+]\x1b[39m Queue");
    browserVisible = true;
    uiPresent();
}

static void chooseThemeStyle(void) {
    unsigned choice = themeStyle;
    while (!quitting) {
        frame(&bottom, "THEME MENU STYLE");
        for (unsigned index = 0; index < 5; index++) {
            int y = 28 + index * 22;
            uiButton(8, y, 240, 21, index == choice ? 8 : 2);
            position(&bottom, (y + 4) / 8 + 1, 4); iprintf("%s %s", index == choice ? ">" : " ", themeStyleLabels[index]);
        }
        uiButton(8, 140, 240, 24, 2);
        position(&bottom, 19, 3); iprintf("[X] Favorites only: %s", themeFavoritesOnly ? "ON" : "OFF");
        uiButton(8, 168, 240, 24, 2);
        position(&bottom, 23, 3); iprintf("[A] Select  [B] Back");
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
        frame(&bottom, activeSystem == SYSTEM_OTHERS ? "SELECT CATEGORY" : "SELECT REGION");
        for (unsigned i = 0; i < options; i++) {
            int y = 28 + i * 22;
            uiButton(8, y, 240, 21, i == choice ? 8 : 2);
            if (i == choice) uiPanel(false, 8, y + 2, 3, 17, 4);
            position(&bottom, (y + 4) / 8 + 1, 4);
            iprintf(i == choice ? "\x1b[36m>\x1b[39m %s" : "\x1b[37m  %s", filterLabel(i));
        }
        uiButton(8, 168, 240, 24, 2);
        position(&bottom, 23, 3); iprintf("\x1b[36m[B]\x1b[39m Cancel   \x1b[37m[A] Select");
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
    position(&bottom, 5, 3); iprintf("\x1b[37mRunning: " APP_VERSION);
    position(&bottom, 7, 3); iprintf("PC build: %08lx", (unsigned long)job.crc);
    position(&bottom, 10, 3); iprintf("\x1b[39mInstall to:");
    position(&bottom, 12, 3); wrapped(appPath, 3);
    position(&bottom, 16, 3); iprintf("\x1b[37m%s", appPathKnown ? "Old app saved as .nds.bak" : "No launch path: SD root copy");
    uiButton(8, 144, 240, 24, 4); uiButton(8, 168, 240, 24, 2);
    position(&bottom, 20, 3); iprintf("\x1b[35m[A] Install update\x1b[39m");
    position(&bottom, 23, 3); iprintf("\x1b[37m[B] Cancel");
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

static void drawQueue(unsigned selectedItem) {
    clear(&top);
    uiIcon(true, 0, 0);
    position(&top, 2, 6); iprintf("\x1b[36mDOWNLOAD QUEUE");
    position(&top, 3, 6); iprintf("\x1b[37m%u remaining / saved on SD", downloads.count);
    if (!downloads.count) {
        position(&top, 9, 3); iprintf("\x1b[39mYour queue is empty.");
        position(&top, 12, 3); iprintf("\x1b[37mAdd games from either tab.");
    }
    unsigned first = selectedItem < 6 ? 0 : selectedItem - 5;
    for (unsigned i = first; i < downloads.count && i < first + 6; i++) {
        const Game *g = &downloads.items[i];
        int y = 32 + (i - first) * 24;
        if (i == selectedItem) uiPanel(true, 4, y, 248, 23, 8);
        else if ((i - first) % 2 == 0) uiPanel(true, 4, y, 248, 23, 2);
        position(&top, y / 8 + 1, 3); iprintf("\x1b[39m%.28s", g->title);
        position(&top, y / 8 + 2, 3); iprintf("\x1b[36m%s\x1b[37m  %s", systemLabel(g->system),
            queueRunning && i == 0 ? "Active" : "Waiting");
    }
    position(&top, 23, 2); iprintf("\x1b[37mUP/DOWN Select  %u/%u", downloads.count ? selectedItem + 1 : 0, downloads.count);
    position(&top, 24, 2); iprintf("\x1b[37mAll systems / one at a time");
    frame(&bottom, "DOWNLOAD QUEUE");
    if (downloads.count) {
        const Game *g = &downloads.items[selectedItem];
        drawGameDetail(g, NULL);
        uiPanel(false, 4, 124, 248, 16, 2);
        position(&bottom, 17, 3); iprintf("\x1b[37mSave to %s/", downloadDirectory(g->system));
    } else {
        uiIcon(false, 112, 36);
        uiTitle("Ready for your next game", 32, 76, 192, 2);
        position(&bottom, 16, 3); iprintf("\x1b[37mAdd games from the catalog.");
    }
    uiButton(4, 144, 122, 23, 2); uiButton(130, 144, 122, 23, 2);
    position(&bottom, 20, 2); iprintf("\x1b[36m[X]\x1b[39m Remove");
    position(&bottom, 20, 18); iprintf("\x1b[36m[B]\x1b[39m Back");
    uiButton(4, 168, 248, 24, downloads.count ? 4 : 3);
    position(&bottom, 23, 6); iprintf(downloads.count ? "\x1b[35m[A] START QUEUE\x1b[39m" : "\x1b[37mQUEUE IS EMPTY");
    uiPresent();
}

static int waitQueueKey(unsigned selectedItem) {
    unsigned idle = 0;
    while (pmMainLoop()) {
        swiWaitForVBlank(); scanKeys();
        clientTick(true);
        if (clientPending()) return KEY_B;
        int pending = deferredKeys; deferredKeys = 0;
        int key = keysDown() | (keysDownRepeat() & (KEY_UP | KEY_DOWN)) | pending;
        if (key & KEY_TOUCH) {
            if (!(pending & KEY_TOUCH)) touchRead(&lastTouch);
            if (!uiTouchFeedback(lastTouch.px, lastTouch.py)) key &= ~KEY_TOUCH;
        }
        if (key) return key;
        if (++idle == 18 && downloads.count) {
            loadCoverPreview(&downloads.items[selectedItem]);
            drawQueue(selectedItem);
        }
    }
    quitting = true;
    return KEY_B;
}

static bool queuedTransfer(const Game *game, void *context) {
    drawQueue(0);
    return prepareAndTransfer(game, context);
}

static void showQueue(void) {
    unsigned selectedItem = 0;
    while (!quitting) {
        drawQueue(selectedItem);
        int key = waitQueueKey(selectedItem);
        if (key & KEY_TOUCH) {
            touchPosition touch = lastTouch;
            if (touch.py >= 144 && touch.py < 168) key |= touch.px < 128 ? KEY_X : KEY_B;
            else if (touch.py >= 168) key |= KEY_A;
        }
        if ((key & KEY_START) && (keysHeld() & KEY_L)) { quitting = true; break; }
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
    while (!quitting) {
        frame(&bottom, "TWILIGHT MENU++ THEME");
        position(&bottom, 5, 3); wrapped(game->title, 3);
        position(&bottom, 9, 3); iprintf("%.25s", game->region);
        position(&bottom, 11, 3); iprintf("Installs to theme folder");
        uiButton(8, 96, 240, 24, 2);
        position(&bottom, 14, 3); iprintf("[Y] Dual-screen preview");
        uiButton(8, 120, 240, 24, 2);
        position(&bottom, 17, 3); iprintf("[X] Favorite: %s", favorite ? "YES" : "NO");
        uiButton(8, 144, 240, 24, 4);
        position(&bottom, 20, 3); iprintf("[A] Install theme");
        uiButton(8, 168, 240, 24, 2);
        position(&bottom, 23, 3); iprintf("[B] Back");
        int key = waitKey();
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
    if (games[selected].system == SYSTEM_OTHERS && !strncmp(games[selected].languages, "Themes", 6)) {
        themeSelected(&games[selected]); return;
    }
    const Game game = games[selected];
    frame(&bottom, "DOWNLOAD TO SD");
    position(&bottom, 5, 3);
    wrapped(game.title, 3);
    position(&bottom, 10, 3); iprintf("\x1b[36m%s", game.region);
    position(&bottom, 13, 3); iprintf("\x1b[37mSave to %s/", downloadDirectory(game.system));
    position(&bottom, 16, 3); iprintf("Partial downloads resume.");
    uiButton(8, 144, 240, 24, 4);
    position(&bottom, 20, 3); iprintf("\x1b[35m[A] Download / resume\x1b[39m");
    uiButton(8, 168, 156, 24, 2); uiButton(168, 168, 80, 24, 2);
    position(&bottom, 23, 3); iprintf("\x1b[37m[X] Add to queue");
    position(&bottom, 23, 23); iprintf("\x1b[37m[B] Back");
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
    if (!prepareAndTransfer(&game, NULL) && !quitting) message("Download", lastError);
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
        swiWaitForVBlank(); scanKeys();
        int pending = deferredKeys; deferredKeys = 0;
        int key = catalogInput(pending);
        if (key & KEY_TOUCH) {
            touchPosition touch;
            if (pending & KEY_TOUCH) touch = lastTouch; else touchRead(&touch);
            if (!uiTouchFeedback(touch.px, touch.py)) continue;
            if (touch.py < 24) key |= touch.px >= 204 ? ACTION_RANDOM : systemTouchAction(touch.px);
            if (touch.py >= 124 && touch.py < 144) key |= ACTION_REGION;
            if (touch.py >= 144 && touch.py < 168) key |= touch.px < 128 ? KEY_X : KEY_START;
            if (touch.py >= 168) key |= touch.px < 128 ? KEY_A : ACTION_ADD_QUEUE;
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
        } else if (key & KEY_SELECT) {
            ConfigChange change = configure();
            if (change == CONFIG_UPDATE && !quitting) {
                if (connectWifi(false)) updateApp();
                else message("Update connection", lastError);
            } else if (change != CONFIG_NONE && !quitting && ((change != CONFIG_FILTER && !connectWifi(change == CONFIG_WIFI)) || !fetchPage(0)))
                message("Connection", lastError);
        } else if (key & KEY_X) {
            if (inputText("SEARCH LIBRARY", query, sizeof(query)) && !fetchPage(0)) message("Search", lastError);
        } else if (key & ACTION_REGION) {
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
        } else if (key & KEY_B) {
            query[0] = 0; regionIndex = 0;
            if (!fetchPage(0)) message("Library", lastError);
        } else if ((key & ACTION_ADD_QUEUE) && count) enqueueGame(&games[selected]);
        else if (key & KEY_A) downloadSelected();
        else if (key & KEY_START) {
            showQueue();
        }
        if (!(key & (KEY_UP | KEY_DOWN))) coverPage(games, count, selected);
    }
    clientClose(); coverPausePrefetch();
    if (wifiInitialized) wlmgrStop();
    return 0;
}
