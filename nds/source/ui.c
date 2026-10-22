#include "ui.h"
#include "icon.h"
#include "cover.h"
#include "title_font.h"
#include "brand_font.h"
#include <stdio.h>
#include <string.h>

static u16 *canvas[2];
static u16 stagedCanvas[2][16384] __attribute__((aligned(32)));
static u16 stagedText[2][1024] __attribute__((aligned(32)));
static PrintConsole *consoles[2];
static int panelBg[2];
static bool dirty[2];
static unsigned visible[2];
static const unsigned tileBases[2][2] = {{4, 6}, {1, 4}};
static const unsigned textBases[2][2] = {{22, 28}, {31, 28}};
static u16 *coverGfx[2][4];
static const SpriteSize coverShapes[4] = {SpriteSize_64x64, SpriteSize_32x64, SpriteSize_64x32, SpriteSize_32x32};
static u16 coverStaging[64 * 64] __attribute__((aligned(32)));
static const u16 *wantedCover, *displayedCover;
static unsigned coverBuffer;
typedef struct { int x, y, width, height; unsigned color; } Button;
static Button buttons[12];
static unsigned buttonCount;
static unsigned paletteBank;
static Keyboard *inputKeyboard;
enum { KEY_ROWS = 5, KEY_HEIGHT = 24, KEY_TILES = 32 * KEY_ROWS * KEY_HEIGHT / 8 };
static u16 keyReleased[KEY_TILES], keyPressed[KEY_TILES];
static u16 keyboardTiles[KEY_TILES * 16 * 2] __attribute__((aligned(32)));
static int renderedState = -1;

static void tilePixel(u16 *base, int x, int y, unsigned color) {
    if ((unsigned)x >= 256 || (unsigned)y >= 192) return;
    unsigned tile = (y / 8) * 32 + x / 8;
    vu16 *word = base + tile * 16 + (y % 8) * 2 + (x % 8) / 4;
    unsigned shift = (x & 3) * 4;
    *word = (*word & ~(15U << shift)) | (color << shift);
}

static void pixel(bool main, int x, int y, unsigned color) { tilePixel(canvas[main], x, y, color); }

static void rectangle(bool main, int x, int y, int width, int height, unsigned color) {
    for (int row = y; row < y + height; row++)
        for (int col = x; col < x + width; col++) pixel(main, col, row, color);
}

void uiPanel(bool main, int x, int y, int width, int height, unsigned color) {

    for (int row = 0; row < height; row++) {
        int edge = row == 0 || row == height - 1 ? 3 :
            row == 1 || row == height - 2 ? 1 : 0;
        rectangle(main, x + edge, y + row, width - edge * 2, 1, color);
    }
}

void uiClear(bool main) {
    memset(canvas[main], 0x11, sizeof(stagedCanvas[main]));
    dirty[main] = true;
    if (!main) { buttonCount = 0; wantedCover = NULL; }
}

void uiInvalidate(bool main) { dirty[main] = true; }

void uiCover(const u16 *pixels) { wantedCover = pixels; dirty[0] = true; }

static void uploadCoverPart(unsigned part) {
    unsigned left = (part & 1) * 64, top = (part >> 1) * 64;
    unsigned width = left ? 32 : 64, height = top ? 32 : 64;
    unsigned columns = COVER_SIZE - left, rows = COVER_SIZE - top;
    if (columns > width) columns = width;
    if (rows > height) rows = height;
    memset(coverStaging, 0, width * height * sizeof(u16));
    for (unsigned y = 0; y < rows; y++)
        memcpy(coverStaging + y * width, wantedCover + (top + y) * COVER_SIZE + left, columns * sizeof(u16));
    DC_FlushRange(coverStaging, width * height * sizeof(u16));
    dmaCopy(coverStaging, coverGfx[coverBuffer][part], width * height * sizeof(u16));
}

void uiPresent(void) {
    bool coverChanged = wantedCover != displayedCover || (dirty[0] && wantedCover);
    if (!dirty[0] && !dirty[1] && !coverChanged) return;
    for (unsigned main = 0; main < 2; main++) {
        if (!dirty[main]) continue;
        unsigned hidden = visible[main] ^ 1;
        u8 *vram = (u8 *)(main ? BG_GFX : BG_GFX_SUB);
        DC_FlushRange(canvas[main], sizeof(stagedCanvas[main]));
        DC_FlushRange(stagedText[main], sizeof(stagedText[main]));
        dmaCopy(canvas[main], vram + tileBases[main][hidden] * 16384, sizeof(stagedCanvas[main]));
        dmaCopy(stagedText[main], vram + textBases[main][hidden] * 2048, sizeof(stagedText[main]));
    }
    if (coverChanged && wantedCover) {
        coverBuffer ^= 1;
        for (unsigned part = 0; part < 4; part++) uploadCoverPart(part);
    }
    swiWaitForVBlank();
    for (unsigned main = 0; main < 2; main++) {
        if (!dirty[main]) continue;
        visible[main] ^= 1;
        bgSetTileBase(panelBg[main], tileBases[main][visible[main]]);
        bgSetMapBase(consoles[main]->bgId, textBases[main][visible[main]]);
        dirty[main] = false;
    }
    if (coverChanged) {
        for (unsigned part = 0; part < 4; part++)
            oamSet(&oamSub, part, 8 + (part & 1) * 64, 32 + (part >> 1) * 64, 1, 15,
                coverShapes[part], SpriteColorFormat_Bmp, coverGfx[coverBuffer][part],
                -1, false, !wantedCover, false, false, false);
        oamUpdate(&oamSub);
        displayedCover = wantedCover;
    }
}

void uiButton(int x, int y, int width, int height, unsigned color) {
    uiPanel(false, x, y, width, height, color);
    if (buttonCount < sizeof(buttons) / sizeof(buttons[0]))
        buttons[buttonCount++] = (Button){x, y, width, height, color};
}

bool uiTouchFeedback(int x, int y) {
    for (unsigned i = 0; i < buttonCount; i++) {
        Button button = buttons[i];
        if (x < button.x || x >= button.x + button.width || y < button.y || y >= button.y + button.height) continue;

        uiPanel(false, button.x, button.y, button.width, button.height, 5);
        uiPanel(false, button.x + 2, button.y + 2, button.width - 4, button.height - 4, button.color);
        dirty[0] = true; uiPresent();

        unsigned frames = 0;
        bool inside = true;
        do {
            swiWaitForVBlank(); scanKeys(); frames++;
            if (keysHeld() & KEY_TOUCH) {
                touchPosition touch; touchRead(&touch);
                inside = touch.px >= button.x && touch.px < button.x + button.width &&
                    touch.py >= button.y && touch.py < button.y + button.height;
            }
        } while (pmMainLoop() && ((keysHeld() & KEY_TOUCH) || frames < 4));
        uiPanel(false, button.x, button.y, button.width, button.height, button.color);
        dirty[0] = true; uiPresent();
        return inside;
    }
    return false;
}

void uiIcon(bool main, int x, int y) {
    for (int row = 0; row < 32; row++)
        for (int col = 0; col < 32; col++) {
            unsigned color = rfIcon[row * 32 + col];
            if (color) pixel(main, x + col, y + row, rfIconGuiColors[color]);
        }
}

static unsigned titleWidth(unsigned char character) {
    return titleWidths[(character >= 32 && character <= 126 ? character : '?') - 32];
}

static void rasterGlyph(u16 *target, unsigned char character, int x, int y, bool pressed) {
    unsigned index = (character >= 32 && character <= 126 ? character : '?') - 32;
    static const unsigned colors[2][4] = {{1, 14, 6, 7}, {4, 6, 14, 1}};
    for (unsigned row = 0; row < 16; row++) for (unsigned col = 0; col < 16; col++) {
        unsigned pixelIndex = row * 16 + col;
        unsigned coverage = (titleGlyphs[index][pixelIndex / 4] >> ((pixelIndex % 4) * 2)) & 3;
        if (coverage) tilePixel(target, x + col, y + row, colors[pressed][coverage]);
    }
}

void uiText(bool main, const char *text, int x, int y, int width, unsigned maxLines) {
    while (*text && maxLines) {
        unsigned length = 0, pixels = 0, breakAt = 0;
        while (text[length] && text[length] != '\n' && pixels + titleWidth(text[length]) <= (unsigned)width) {
            if (text[length] == ' ') breakAt = length;
            pixels += titleWidth(text[length++]);
        }
        if (!length) break;
        bool truncated = maxLines == 1 && text[length];
        if (text[length] && text[length] != ' ' && text[length] != '\n' && breakAt && !truncated) length = breakAt;
        if (truncated) {
            unsigned used = 0;
            for (unsigned i = 0; i < length; i++) used += titleWidth(text[i]);
            while (length && used + 3 * titleWidth('.') > (unsigned)width) used -= titleWidth(text[--length]);
        }
        int left = x;
        for (unsigned i = 0; i < length; i++) { rasterGlyph(canvas[main], text[i], left, y, false); left += titleWidth(text[i]); }
        if (truncated) for (unsigned i = 0; i < 3; i++) { rasterGlyph(canvas[main], '.', left, y, false); left += titleWidth('.'); }
        text += length;
        if (*text == '\n') text++;
        while (*text == ' ') text++;
        y += 14; maxLines--;
    }
    dirty[main] = true;
}

void uiTitle(const char *text, int x, int y, int width, unsigned maxLines) { uiText(false, text, x, y, width, maxLines); }

void uiBrand(bool includeLogo) {
    uiPanel(true, 8, 8, 240, 176, 2);
    uiPanel(true, 10, 10, 236, 172, 1);
    uiPanel(true, 74, 16, 108, 80, 8);
    uiPanel(true, 82, 20, 92, 72, 2);
    if (includeLogo) for (unsigned row = 0; row < 32; row++) for (unsigned col = 0; col < 32; col++) {
        unsigned color = rfIcon[row * 32 + col];
        if (color) rectangle(true, 96 + col * 2, 24 + row * 2, 2, 2, rfIconGuiColors[color]);
    }
    const char *label = APP_NAME;
    unsigned width = 0;
    for (unsigned i = 0; label[i]; i++) width += brandWidths[strchr(brandChars, label[i]) - brandChars];
    int left = (256 - (int)width) / 2;

    const unsigned colors[] = {1, 14, 6, 7};
    for (unsigned i = 0; label[i]; i++) {
        unsigned glyph = strchr(brandChars, label[i]) - brandChars;
        for (unsigned row = 0; row < 32; row++) for (unsigned col = 0; col < 32; col++) {
            unsigned p = row * 32 + col;
            unsigned coverage = (brandGlyphs[glyph][p / 4] >> ((p % 4) * 2)) & 3;
            if (coverage) pixel(true, left + col, 92 + row, colors[coverage]);
        }
        left += brandWidths[glyph];
    }
    rectangle(true, 104, 126, 48, 2, 4);
    dirty[1] = true;
}

void uiStageMark(int x, int y, unsigned state) {
    uiPanel(false, x, y, 16, 16, state == 2 ? 4 : state == 1 ? 8 : 3);
    if (state == 2) {
        for (unsigned i = 0; i < 3; i++) rectangle(false, x + 3 + i, y + 7 + i, 2, 2, 1);
        for (unsigned i = 0; i < 6; i++) rectangle(false, x + 5 + i, y + 9 - i, 2, 2, 1);
    } else if (state == 1) uiPanel(false, x + 4, y + 4, 8, 8, 4);
}

void uiBusy(unsigned phase) {
    rectangle(false, 8, 160, 240, 3, 3);
    unsigned start = phase % 280;
    for (unsigned i = 0; i < 40; i++) {
        int x = (int)start - 40 + i;
        if (x >= 0 && x < 240) rectangle(false, 8 + x, 160, 1, 3, 4);
    }
}

void uiChevron(int x, int y) {
    for (unsigned row = 0; row < 4; row++) rectangle(false, x + row, y + row, 7 - row * 2, 1, 4);
}

static void largeDigit(int x, int y, char digit) {
    static const unsigned char glyphs[11][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14}, {17,2,4,8,16,17,0},
    };
    unsigned index = digit == '%' ? 10 : (unsigned)(digit - '0');
    if (index > 10) return;
    for (unsigned row = 0; row < 7; row++)
        for (unsigned col = 0; col < 5; col++)
            if (glyphs[index][row] & (16 >> col)) rectangle(false, x + col * 3, y + row * 3, 3, 3, 4);
}

void uiMeter(unsigned percent) {
    if (percent > 100) percent = 100;
    char value[5];
    snprintf(value, sizeof(value), "%u%%", percent);
    unsigned length = strlen(value);
    int x = (256 - (int)length * 18 + 3) / 2;
    for (unsigned i = 0; i < length; i++) largeDigit(x + i * 18, 108, value[i]);
    uiPanel(false, 16, 140, 224, 8, 3);
    unsigned filled = 224 * percent / 100;
    if (filled) rectangle(false, 16, 142, filled, 4, 4);
}

static const char *keyLabel(int key, char *letter) {
    switch (key) {
        case DVK_BACKSPACE: return "DEL";
        case DVK_ENTER: return "OK";
        case DVK_CAPS: return "CAPS";
        case DVK_SHIFT: return "SHIFT";
        case DVK_SPACE: return "SPACE";
        case DVK_CTRL: return "CTRL";
        case DVK_ALT: return "ALT";
        case DVK_MENU: return "MENU";
        case DVK_TAB: return "TAB";
        case DVK_FOLD: case DVK_UP: return "^";
        case DVK_DOWN: return "v";
        case DVK_LEFT: return "<";
        case DVK_RIGHT: return ">";
        default:
            letter[0] = key >= 32 && key <= 126 ? key : ' ';
            letter[1] = 0;
            return letter;
    }
}

void uiKeyboardSync(void) {
    if (renderedState == (int)inputKeyboard->state) return;
    renderedState = inputKeyboard->state;
    const KeyMap *layout = inputKeyboard->mappings[renderedState];

    for (unsigned pressed = 0; pressed < 2; pressed++) {
        u16 *tiles = keyboardTiles + pressed * KEY_TILES * 16;
        memset(tiles, 0x11, KEY_TILES * 32);
        for (int row = 0; row < 5; row++) {
            for (int start = 0; start < 32;) {
                int key = layout->keymap[row * 32 + start];
                int end = start + 1;
                while (end < 32 && layout->keymap[row * 32 + end] == key) end++;
                int left = start * 8, width = (end - start) * 8;
                if (key != NOKEY) {
                    for (int y = 1; y < KEY_HEIGHT - 1; y++)
                        for (int x = 1; x < width - 1; x++)
                            tilePixel(tiles, left + x, row * KEY_HEIGHT + y, pressed ? 4 : y == 1 ? 3 : 2);
                    char letter[2];
                    const char *label = keyLabel(key, letter);
                    unsigned length = strlen(label);
                    unsigned labelWidth = 0;
                    for (unsigned c = 0; c < length; c++) labelWidth += titleWidth(label[c]);
                    if (labelWidth > (unsigned)width - 2) { length = 1; labelWidth = titleWidth(label[0]); }
                    int labelX = left + (width - (int)labelWidth) / 2;
                    for (unsigned c = 0; c < length; c++) {
                        rasterGlyph(tiles, label[c], labelX, row * KEY_HEIGHT + 4, pressed);
                        labelX += titleWidth(label[c]);
                    }
                }
                start = end;
            }
        }
    }
    DC_FlushRange(keyboardTiles, sizeof(keyboardTiles));
    swiWaitForVBlank();
    dmaCopy(keyboardTiles, bgGetGfxPtr(inputKeyboard->background), sizeof(keyboardTiles));
}

void uiInit(PrintConsole *top, PrintConsole *bottom, Keyboard *keyboard) {

    bool used[16] = {0};
    for (unsigned layout = 0; layout < 4; layout++) {
        const KeyMap *map = keyboard->mappings[layout];
        if (!map) continue;
        unsigned tiles = map->width * map->height * keyboard->grid_width * keyboard->grid_height / 64;
        for (unsigned i = 0; i < tiles; i++) {
            used[map->mapDataReleased[i] >> 12] = true;
            used[map->mapDataPressed[i] >> 12] = true;
        }
    }
    paletteBank = 15;
    while (paletteBank && used[paletteBank]) paletteBank--;
    for (unsigned main = 0; main < 2; main++) {
        int bg = main ? bgInit(1, BgType_Text4bpp, BgSize_T_256x256, 30, 1) :
            bgInitSub(1, BgType_Text4bpp, BgSize_T_256x256, 23, 4);
        panelBg[main] = bg;
        consoles[main] = main ? top : bottom;
        memcpy(stagedText[main], consoles[main]->fontBgMap, sizeof(stagedText[main]));
        consoles[main]->fontBgMap = stagedText[main];
        canvas[main] = stagedCanvas[main];
        vu16 *map = bgGetMapPtr(bg);
        for (unsigned tile = 0; tile < 1024; tile++) map[tile] = tile | (paletteBank << 12);
        bgSetPriority(bg, 2);
        // Palette/VRAM reject byte stores. Volatile halfwords also prevent GCC
        // from replacing the palette loop with an unsafe byte-copy routine.
        vu16 *palette = main ? BG_PALETTE : BG_PALETTE_SUB;
        palette[0] = rfPalette[1];
        for (unsigned color = 1; color < 15; color++) palette[paletteBank * 16 + color] = rfPalette[color];

        const unsigned fontColors[8] = {7,11,4,5,12,1,4,6};
        for (unsigned color = 0; color < 16; color++) palette[color * 16 + 15] = rfPalette[fontColors[color % 8]];
        palette[255] = rfPalette[7];
        uiClear(main);
    }
    bgSetPriority(top->bgId, 1);
    bgSetPriority(bottom->bgId, 1);

    vramSetBankD(VRAM_D_SUB_SPRITE);
    oamInit(&oamSub, SpriteMapping_Bmp_1D_128, false);
    for (unsigned i = 0; i < 2; i++) for (unsigned part = 0; part < 4; part++)
        coverGfx[i][part] = oamAllocateGfx(&oamSub, coverShapes[part], SpriteColorFormat_Bmp);
    inputKeyboard = keyboard;
    keyboard->grid_height = KEY_HEIGHT;
    keyboard->offset_y = -(192 - KEY_ROWS * KEY_HEIGHT);
    for (unsigned tile = 0; tile < KEY_TILES; tile++) {
        keyReleased[tile] = tile | (paletteBank << 12);
        keyPressed[tile] = (tile + KEY_TILES) | (paletteBank << 12);
    }

    DC_FlushRange(keyReleased, sizeof(keyReleased));
    DC_FlushRange(keyPressed, sizeof(keyPressed));
    for (unsigned layout = 0; layout < 2; layout++) {
        keyboard->mappings[layout]->mapDataReleased = keyReleased;
        keyboard->mappings[layout]->mapDataPressed = keyPressed;
    }
    uiKeyboardSync();
    dmaCopy(keyReleased, bgGetMapPtr(keyboard->background), sizeof(keyReleased));
}
