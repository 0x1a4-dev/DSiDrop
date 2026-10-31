#include "ui.h"
#include "icon.h"
#include "cover.h"
#include "cover_motion.h"
#include "title_font.h"
#include "brand_font.h"
#include "ui_art.h"
#include "ui_font.h"
#include <stdio.h>
#include <stdlib.h>
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
static u16 *coverGfx[2][2][4];
static const SpriteSize coverShapes[4] = {SpriteSize_64x64, SpriteSize_64x64, SpriteSize_64x64, SpriteSize_64x64};
static u16 coverStaging[64 * 64] __attribute__((aligned(32)));
static const u16 *wantedCover, *displayedCover;
static uint32_t wantedCoverRevision, displayedCoverRevision, outgoingCoverRevision;
static const u16 *outgoingCover;
static bool wantedMain, displayedMain;
static u16 *neighbourGfx[2][2];
static const u16 *wantedNeighbours[2], *displayedNeighbours[2];
static uint32_t wantedNeighbourRevisions[2], displayedNeighbourRevisions[2];
static unsigned neighbourBuffer;
static unsigned coverBuffer;
static unsigned slideRemaining, outgoingBuffer;
static int slideDirection, requestedSlide;
static CoverMotion incomingMotion, outgoingMotion;
static unsigned departingNeighbourBuffer;
static bool departingNeighbourVisible;
typedef struct { int x, y, width, height; unsigned color; } Button;
static Button buttons[24];
static unsigned buttonCount;
static unsigned paletteBank;
static Keyboard *inputKeyboard;
enum { KEY_ROWS = 5, KEY_HEIGHT = 24, KEY_TILES = 32 * KEY_ROWS * KEY_HEIGHT / 8 };
static u16 keyReleased[KEY_TILES], keyPressed[KEY_TILES];
static u16 keyboardTiles[KEY_TILES * 16 * 2] __attribute__((aligned(32)));
static int renderedState = -1;
static u16 *previewBackup;
static uint32_t previewModes[2];
static uint16_t previewBgControl[2];
static void positionTopCover(void);

bool uiThemePreview(const u16 *pixels) {
    previewBackup = malloc(2 * 128 * 1024);
    if (!previewBackup) return false;
    slideRemaining = 0;
    uiPresent();
    positionTopCover();
    oamUpdate(&oamMain);
    memcpy(previewBackup, BG_GFX, 128 * 1024);
    memcpy(previewBackup + 65536, BG_GFX_SUB, 128 * 1024);
    previewModes[0] = REG_DISPCNT; previewModes[1] = REG_DISPCNT_SUB;
    previewBgControl[0] = REG_BG3CNT; previewBgControl[1] = REG_BG3CNT_SUB;
    swiWaitForVBlank();
    REG_DISPCNT = MODE_5_2D | DISPLAY_BG3_ACTIVE;
    REG_DISPCNT_SUB = MODE_5_2D | DISPLAY_BG3_ACTIVE;
    REG_BG3CNT = BG_BMP16_256x256 | BG_BMP_BASE(0);
    REG_BG3CNT_SUB = BG_BMP16_256x256 | BG_BMP_BASE(0);
    REG_BG3PA = REG_BG3PD = REG_BG3PA_SUB = REG_BG3PD_SUB = 256;
    REG_BG3PB = REG_BG3PC = REG_BG3PB_SUB = REG_BG3PC_SUB = 0;
    REG_BG3X = REG_BG3Y = REG_BG3X_SUB = REG_BG3Y_SUB = 0;
    dmaCopy(pixels, BG_GFX, 256 * 192 * 2);
    dmaCopy(pixels + 256 * 192, BG_GFX_SUB, 256 * 192 * 2);
    return true;
}

void uiThemePreviewEnd(void) {
    if (!previewBackup) return;
    DC_FlushRange(previewBackup, 2 * 128 * 1024);
    swiWaitForVBlank();
    dmaCopy(previewBackup, BG_GFX, 128 * 1024);
    dmaCopy(previewBackup + 65536, BG_GFX_SUB, 128 * 1024);
    REG_BG3CNT = previewBgControl[0]; REG_BG3CNT_SUB = previewBgControl[1];
    REG_DISPCNT = previewModes[0]; REG_DISPCNT_SUB = previewModes[1];
    free(previewBackup); previewBackup = NULL;
}

static void tilePixel(u16 *base, int x, int y, unsigned color) {
    if ((unsigned)x >= 256 || (unsigned)y >= 192) return;
    unsigned tile = (y / 8) * 32 + x / 8;
    vu16 *word = base + tile * 16 + (y % 8) * 2 + (x % 8) / 4;
    unsigned shift = (x & 3) * 4;
    *word = (*word & ~(15U << shift)) | (color << shift);
}

static void pixel(bool main, int x, int y, unsigned color) { tilePixel(canvas[main], x, y, color); }

// Glyphs and symbols carry real 2-bit coverage. Blend with the pixel already
// underneath them, so muted labels and violet buttons keep their own ink.
static u8 coverageColors[16][16][4];

static void prepareCoverageColors(void) {
    for (unsigned background = 0; background < 16; background++)
        for (unsigned ink = 0; ink < 16; ink++) {
            coverageColors[background][ink][0] = background;
            coverageColors[background][ink][3] = ink;
            for (unsigned coverage = 1; coverage < 3; coverage++) {
                int rgb[3];
                for (unsigned channel = 0; channel < 3; channel++) {
                    unsigned shift = channel * 5;
                    rgb[channel] = (((rfPalette[background] >> shift) & 31) * (3 - coverage) +
                        ((rfPalette[ink] >> shift) & 31) * coverage + 1) / 3;
                }
                unsigned best = background ? background : 1, distance = ~0U;
                // Index 0 is transparent; index 15 belongs to console text.
                for (unsigned candidate = 1; candidate < 15; candidate++) {
                    int red = (int)(rfPalette[candidate] & 31) - rgb[0];
                    int green = (int)((rfPalette[candidate] >> 5) & 31) - rgb[1];
                    int blue = (int)((rfPalette[candidate] >> 10) & 31) - rgb[2];
                    unsigned score = red * red * 2 + green * green * 3 + blue * blue;
                    if (score < distance) { best = candidate; distance = score; }
                }
                coverageColors[background][ink][coverage] = best;
            }
        }
}

static void coveragePixel(u16 *target, int x, int y, unsigned coverage, unsigned color) {
    if (!coverage || (unsigned)x >= 256 || (unsigned)y >= 192) return;
    if (coverage == 3) { tilePixel(target, x, y, color); return; }
    unsigned tile = (y / 8) * 32 + x / 8;
    unsigned shift = (x & 3) * 4;
    unsigned background = (target[tile * 16 + (y % 8) * 2 + (x % 8) / 4] >> shift) & 15;
    tilePixel(target, x, y, coverageColors[background][color & 15][coverage]);
}

static void aaPixel(bool main, int x, int y, unsigned coverage, unsigned color) {
    coveragePixel(canvas[main], x, y, coverage, color);
}

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

static void artwork(bool main, const u16 *runs) {
    unsigned cursor = 0;
    while (runs[0] && cursor + runs[0] <= 16384) {
        for (unsigned count = 0; count < runs[0]; count++) canvas[main][cursor++] = runs[1];
        runs += 2;
    }
    dirty[main] = true;
}

void uiFeatureBackdrop(void) { artwork(true, uiArtFeature); }

void uiClear(bool main) {
    memset(canvas[main], 0x11, sizeof(stagedCanvas[main]));
    artwork(main, main ? uiArtTop : uiArtBottom);
    if (!main) buttonCount = 0;
    if (main == wantedMain) { wantedCover = NULL; wantedCoverRevision = 0; }
    if (main) {
        wantedNeighbours[0] = wantedNeighbours[1] = NULL;
        wantedNeighbourRevisions[0] = wantedNeighbourRevisions[1] = 0;
    }
}

void uiInvalidate(bool main) { dirty[main] = true; }

void uiCover(const u16 *pixels) {
    wantedCover = pixels; wantedCoverRevision = coverRevision(pixels); wantedMain = false; dirty[0] = true;
}

void uiTopCover(const u16 *pixels) {
    wantedCover = pixels; wantedCoverRevision = coverRevision(pixels); wantedMain = true; dirty[1] = true;
}

void uiTopNeighbours(const u16 *left, const u16 *right) {
    wantedNeighbours[0] = left; wantedNeighbours[1] = right; dirty[1] = true;
    wantedNeighbourRevisions[0] = coverRevision(left); wantedNeighbourRevisions[1] = coverRevision(right);
}

void uiCoverTransition(int direction) { requestedSlide = direction < 0 ? -1 : direction > 0 ? 1 : 0; }

static void positionCoverGroup(unsigned first, unsigned buffer, CoverPose pose, unsigned matrix, unsigned priority) {
    int inverse = COVER_SIZE * 256 * 256 / pose.size;
    oamRotateScale(&oamMain, matrix, 0, inverse, inverse);
    int stride = 64 * 256 / inverse;
    int left = (pose.x + 32 * pose.size / COVER_SIZE + 128) / 256 - 32;
    int top = (pose.y + 32 * pose.size / COVER_SIZE + 128) / 256 - 32;
    for (unsigned part = 0; part < 4; part++) {
        /* Use the hardware's quantized scale for chunk spacing. Rounding each
         * centre independently can open a 1px crack at fractional sizes. */
        int x = left + (int)(part & 1) * stride;
        int y = top + (int)(part >> 1) * stride;
        oamSet(&oamMain, first + part, x, y, priority, 15, coverShapes[part], SpriteColorFormat_Bmp,
            coverGfx[1][buffer][part], matrix, false, false, false, false, false);
    }
}

static void positionTopCover(void) {
    if (displayedCover && displayedMain)
        positionCoverGroup(0, coverBuffer, slideRemaining ? coverMotionPose(&incomingMotion) : coverMotionCenter(), 0, 0);
    else for (unsigned part = 0; part < 4; part++) oamClearSprite(&oamMain, part);
    if (slideRemaining) positionCoverGroup(6, outgoingBuffer, coverMotionPose(&outgoingMotion), 1, 1);
    else for (unsigned part = 0; part < 4; part++) oamClearSprite(&oamMain, 6 + part);

    int travel = slideRemaining ? (int)coverMotionEase(incomingMotion.frame) : 4096;
    unsigned arriving = slideDirection > 0 ? 1 : 0;
    for (unsigned side = 0; side < 2; side++) {
        bool replacedByOutgoing = slideRemaining && side != arriving &&
            displayedNeighbours[side] == outgoingCover && displayedNeighbourRevisions[side] == outgoingCoverRevision;
        if (!displayedNeighbours[side] || replacedByOutgoing) { oamClearSprite(&oamMain, 4 + side); continue; }
        int x = side ? 188 : 4;
        if (slideRemaining && side == arriving) x += slideDirection * 84 * (4096 - travel) / 4096;
        oamSet(&oamMain, 4 + side, x, 48, 2, 15, SpriteSize_64x64, SpriteColorFormat_Bmp,
            neighbourGfx[neighbourBuffer][side], -1, false, false, false, false, false);
    }
    if (slideRemaining && departingNeighbourVisible) {
        unsigned side = arriving ^ 1;
        int x = (side ? 188 : 4) - slideDirection * 84 * travel / 4096;
        oamSet(&oamMain, 10, x, 48, 2, 15, SpriteSize_64x64, SpriteColorFormat_Bmp,
            neighbourGfx[departingNeighbourBuffer][side], -1, false, false, false, false, false);
    } else oamClearSprite(&oamMain, 10);
}

void uiAnimate(void) {
    if (!slideRemaining || previewBackup) return;
    slideRemaining--;
    coverMotionStep(&incomingMotion); coverMotionStep(&outgoingMotion);
    positionTopCover();
    oamUpdate(&oamMain);
}

static void uploadCoverPart(unsigned part) {
    unsigned left = (part & 1) * 64, top = (part >> 1) * 64;
    unsigned width = 64, height = 64;
    unsigned columns = COVER_SIZE - left, rows = COVER_SIZE - top;
    if (columns > width) columns = width;
    if (rows > height) rows = height;
    memset(coverStaging, 0, width * height * sizeof(u16));
    for (unsigned y = 0; y < rows; y++)
        memcpy(coverStaging + y * width, wantedCover + (top + y) * COVER_SIZE + left, columns * sizeof(u16));
    DC_FlushRange(coverStaging, width * height * sizeof(u16));
    dmaCopy(coverStaging, coverGfx[wantedMain][coverBuffer][part], width * height * sizeof(u16));
}

static u16 thumbnailPixel(const u16 *pixels, unsigned x, unsigned y) {
    unsigned sourceX = (2 * x + 1) * COVER_SIZE * 2 - 128;
    unsigned sourceY = (2 * y + 1) * COVER_SIZE * 2 - 128;
    unsigned left = sourceX >> 8, top = sourceY >> 8;
    unsigned fx = sourceX & 255, fy = sourceY & 255;
    unsigned weights[4] = {(256 - fx) * (256 - fy), fx * (256 - fy), (256 - fx) * fy, fx * fy};
    unsigned alpha = 0, red = 0, green = 0, blue = 0;
    for (unsigned part = 0; part < 4; part++) {
        u16 sample = pixels[(top + (part >> 1)) * COVER_SIZE + left + (part & 1)];
        if (!(sample & 0x8000)) continue;
        unsigned weight = weights[part]; alpha += weight;
        red += (sample & 31) * weight; green += ((sample >> 5) & 31) * weight; blue += ((sample >> 10) & 31) * weight;
    }
    if (alpha < 32768) return 0;
    return 0x8000 | ((red + alpha / 2) / alpha) | (((green + alpha / 2) / alpha) << 5) | (((blue + alpha / 2) / alpha) << 10);
}

void uiPresent(void) {
    bool coverChanged = wantedCover != displayedCover || wantedMain != displayedMain || wantedCoverRevision != displayedCoverRevision;
    bool differentCover = coverChanged;
    bool neighboursChanged = wantedNeighbours[0] != displayedNeighbours[0] || wantedNeighbours[1] != displayedNeighbours[1] ||
        wantedNeighbourRevisions[0] != displayedNeighbourRevisions[0] || wantedNeighbourRevisions[1] != displayedNeighbourRevisions[1];
    if (!dirty[0] && !dirty[1] && !coverChanged && !neighboursChanged) return;
    if (differentCover) {
        CoverPose previous = slideRemaining ? coverMotionPose(&incomingMotion) : coverMotionCenter();
        bool reversing = slideRemaining && wantedCover == outgoingCover && wantedCoverRevision == outgoingCoverRevision;
        CoverPose incoming = reversing ? coverMotionPose(&outgoingMotion) : coverMotionSide(requestedSlide);
        outgoingBuffer = coverBuffer;
        outgoingCover = displayedCover; outgoingCoverRevision = displayedCoverRevision;
        slideRemaining = requestedSlide && wantedCover && displayedCover && wantedMain && displayedMain ? COVER_MOTION_FRAMES : 0;
        slideDirection = requestedSlide;
        coverMotionStart(&incomingMotion, incoming, coverMotionCenter());
        coverMotionStart(&outgoingMotion, previous, coverMotionSide(-slideDirection));
        departingNeighbourBuffer = neighbourBuffer;
        departingNeighbourVisible = slideRemaining && displayedNeighbours[slideDirection > 0 ? 0 : 1];
    }
    requestedSlide = 0;
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
        if (differentCover || !slideRemaining) coverBuffer ^= 1;
        for (unsigned part = 0; part < 4; part++) {
            if (!coverGfx[wantedMain][coverBuffer][part])
                coverGfx[wantedMain][coverBuffer][part] = oamAllocateGfx(wantedMain ? &oamMain : &oamSub, coverShapes[part], SpriteColorFormat_Bmp);
            if (!coverGfx[wantedMain][coverBuffer][part]) { wantedCover = NULL; slideRemaining = 0; break; }
        }
        if (wantedCover) for (unsigned part = 0; part < 4; part++) uploadCoverPart(part);
    }
    if (neighboursChanged) {
        neighbourBuffer ^= 1;
        if (neighbourBuffer == departingNeighbourBuffer) departingNeighbourVisible = false;
        for (unsigned side = 0; side < 2; side++) {
            if (!wantedNeighbours[side]) continue;
            if (!neighbourGfx[neighbourBuffer][side])
                neighbourGfx[neighbourBuffer][side] = oamAllocateGfx(&oamMain, SpriteSize_64x64, SpriteColorFormat_Bmp);
            if (!neighbourGfx[neighbourBuffer][side]) { wantedNeighbours[side] = NULL; continue; }
            for (unsigned row = 0; row < 64; row++) for (unsigned col = 0; col < 64; col++)
                coverStaging[row * 64 + col] = thumbnailPixel(wantedNeighbours[side], col, row);
            DC_FlushRange(coverStaging, sizeof(coverStaging));
            dmaCopy(coverStaging, neighbourGfx[neighbourBuffer][side], sizeof(coverStaging));
        }
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
        for (unsigned main = 0; main < 2; main++) {
            OamState *oam = main ? &oamMain : &oamSub;
            if (main) continue;
            for (unsigned part = 0; part < 4; part++) {
                if (!wantedCover || main != wantedMain) { oamClearSprite(oam, part); continue; }
                oamSet(oam, part, (main ? 72 : 8) + (part & 1) * 64, (main ? 24 : 32) + (part >> 1) * 64, 1, 15,
                    coverShapes[part], SpriteColorFormat_Bmp, coverGfx[main][coverBuffer][part],
                    -1, false, false, false, false, false);
            }
            oamUpdate(oam);
        }
        displayedCover = wantedCover; displayedMain = wantedMain; displayedCoverRevision = wantedCoverRevision;
    }
    if (neighboursChanged) {
        for (unsigned side = 0; side < 2; side++) {
            displayedNeighbours[side] = wantedNeighbours[side];
            displayedNeighbourRevisions[side] = wantedNeighbourRevisions[side];
        }
    }
    if (coverChanged || neighboursChanged) {
        positionTopCover();
        oamUpdate(&oamMain);
    }
}

void uiButton(int x, int y, int width, int height, unsigned color) {
    uiPanel(false, x, y, width, height, color);
    if (buttonCount < sizeof(buttons) / sizeof(buttons[0]))
        buttons[buttonCount++] = (Button){x, y, width, height, color};
}

static void buttonOutline(const Button *button, unsigned color) {
    for (int row = 0; row < button->height; row++) {
        int edge = row == 0 || row == button->height - 1 ? 3 :
            row == 1 || row == button->height - 2 ? 1 : 0;
        int left = button->x + edge, right = button->x + button->width - edge - 1;
        if (row == 0 || row == button->height - 1)
            rectangle(false, left, button->y + row, right - left + 1, 1, color);
        else {
            pixel(false, left, button->y + row, color);
            pixel(false, right, button->y + row, color);
        }
    }
}

bool uiTouchFeedback(int x, int y) {
    for (unsigned i = 0; i < buttonCount; i++) {
        Button button = buttons[i];
        if (x < button.x || x >= button.x + button.width || y < button.y || y >= button.y + button.height) continue;

        buttonOutline(&button, 5);
        dirty[0] = true; uiPresent();

        unsigned frames = 0;
        bool inside = true;
        do {
            swiWaitForVBlank(); scanKeys(); uiAnimate(); frames++;
            if (keysHeld() & KEY_TOUCH) {
                touchPosition touch; touchRead(&touch);
                inside = touch.px >= button.x && touch.px < button.x + button.width &&
                    touch.py >= button.y && touch.py < button.y + button.height;
            }
        } while (pmMainLoop() && ((keysHeld() & KEY_TOUCH) || frames < 4));
        buttonOutline(&button, button.color);
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

void uiSymbol(bool main, int x, int y, UiSymbol symbol, unsigned color) {
    if ((unsigned)symbol >= UI_SYMBOL_COUNT) return;
    for (unsigned row = 0; row < 16; row++)
        for (unsigned col = 0; col < 16; col++) {
            unsigned offset = row * 16 + col;
            unsigned coverage = (uiArtIcons[symbol][offset / 4] >> ((offset % 4) * 2)) & 3;
            if (coverage) aaPixel(main, x + col, y + row, coverage, color);
        }
    dirty[main] = true;
}

static unsigned titleWidth(unsigned char character) {
    return titleWidths[(character >= 32 && character <= 126 ? character : '?') - 32];
}

static void rasterGlyph(u16 *target, unsigned char character, int x, int y, bool pressed) {
    unsigned index = (character >= 32 && character <= 126 ? character : '?') - 32;
    for (unsigned row = 0; row < 16; row++) for (unsigned col = 0; col < 16; col++) {
        unsigned pixelIndex = row * 16 + col;
        unsigned coverage = (titleGlyphs[index][pixelIndex / 4] >> ((pixelIndex % 4) * 2)) & 3;
        coveragePixel(target, x + col, y + row, coverage, pressed ? 1 : 7);
    }
}

static unsigned fontWidth(unsigned char character, unsigned style) {
    unsigned index = (character >= 32 && character <= 126 ? character : '?') - 32;
    return style == 1 ? captionWidths[index] : style == 2 ? headingWidths[index] : titleWidths[index];
}

static void fontGlyph(bool main, unsigned char character, int x, int y, unsigned style, unsigned color) {
    unsigned index = (character >= 32 && character <= 126 ? character : '?') - 32;
    const u8 *glyph = style == 1 ? captionGlyphs[index] : style == 2 ? headingGlyphs[index] : titleGlyphs[index];
    unsigned cell = style == 2 ? 24 : 16;
    for (unsigned row = 0; row < cell; row++) for (unsigned col = 0; col < cell; col++) {
        unsigned offset = row * cell + col;
        unsigned coverage = (glyph[offset / 4] >> ((offset % 4) * 2)) & 3;
        aaPixel(main, x + col, y + row, coverage, color);
    }
}

static void textLayout(bool main, const char *text, int x, int y, int width, unsigned maxLines, unsigned style, unsigned color, bool centered) {
    if (width <= 0) return;
    while (*text && maxLines) {
        unsigned length = 0, pixels = 0, breakAt = 0;
        while (text[length] && text[length] != '\n' && pixels + fontWidth(text[length], style) <= (unsigned)width) {
            if (text[length] == ' ') breakAt = length;
            pixels += fontWidth(text[length++], style);
        }
        if (!length) break;
        bool truncated = maxLines == 1 && text[length];
        if (text[length] && text[length] != ' ' && text[length] != '\n' && breakAt && !truncated) length = breakAt;
        if (truncated) {
            unsigned used = 0;
            for (unsigned i = 0; i < length; i++) used += fontWidth(text[i], style);
            while (length && used + 3 * fontWidth('.', style) > (unsigned)width) used -= fontWidth(text[--length], style);
        }
        unsigned lineWidth = truncated ? 3 * fontWidth('.', style) : 0;
        for (unsigned index = 0; index < length; index++) lineWidth += fontWidth(text[index], style);
        int left = x + (centered ? (width - (int)lineWidth) / 2 : 0);
        for (unsigned i = 0; i < length; i++) { fontGlyph(main, text[i], left, y, style, color); left += fontWidth(text[i], style); }
        if (truncated) for (unsigned i = 0; i < 3; i++) { fontGlyph(main, '.', left, y, style, color); left += fontWidth('.', style); }
        text += length;
        if (*text == '\n') text++;
        while (*text == ' ') text++;
        y += style == 2 ? 20 : style == 1 ? 11 : 14; maxLines--;
    }
    dirty[main] = true;
}

void uiText(bool main, const char *text, int x, int y, int width, unsigned maxLines) { textLayout(main, text, x, y, width, maxLines, 0, 7, false); }
unsigned uiTextWidth(const char *text) {
    unsigned width = 0;
    while (*text && *text != '\n') width += fontWidth(*text++, 0);
    return width;
}
void uiHeading(bool main, const char *text, int x, int y, int width) { textLayout(main, text, x, y, width, 1, 2, 7, false); }
void uiCaption(bool main, const char *text, int x, int y, int width, unsigned color) { textLayout(main, text, x, y, width, 1, 1, color, false); }
void uiCenteredCaption(bool main, const char *text, int x, int y, int width, unsigned color) { textLayout(main, text, x, y, width, 1, 1, color, true); }
void uiCenteredText(bool main, const char *text, int x, int y, int width, unsigned maxLines) { textLayout(main, text, x, y, width, maxLines, 0, 7, true); }

void uiListRow(int y, const char *text, unsigned number, bool selected) {
    uiButton(8, y, 240, 21, selected ? 8 : 1);
    if (selected) uiPanel(false, 8, y + 4, 2, 13, 4);
    char label[8]; snprintf(label, sizeof(label), "%02u", number % 100);
    uiCaption(false, label, 18, y + 4, 20, selected ? 5 : 14);
    textLayout(false, text, 42, y + 2, 194, 1, 0, selected ? 7 : 6, false);
}

void uiTitle(const char *text, int x, int y, int width, unsigned maxLines) { uiText(false, text, x, y, width, maxLines); }

void uiBrand(bool includeLogo) {
    if (includeLogo) for (unsigned row = 0; row < 32; row++) for (unsigned col = 0; col < 32; col++) {
        unsigned color = rfIcon[row * 32 + col];
        if (color) rectangle(true, 96 + col * 2, 24 + row * 2, 2, 2, rfIconGuiColors[color]);
    }
    const char *label = APP_NAME;
    unsigned width = 0;
    for (unsigned i = 0; label[i]; i++) width += brandWidths[strchr(brandChars, label[i]) - brandChars];
    int left = (256 - (int)width) / 2;

    for (unsigned i = 0; label[i]; i++) {
        unsigned glyph = strchr(brandChars, label[i]) - brandChars;
        for (unsigned row = 0; row < 32; row++) for (unsigned col = 0; col < 32; col++) {
            unsigned p = row * 32 + col;
            unsigned coverage = (brandGlyphs[glyph][p / 4] >> ((p % 4) * 2)) & 3;
            aaPixel(true, left + col, 92 + row, coverage, 7);
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
    unsigned index = digit == '%' ? 10 : (unsigned)(digit - '0');
    if (index > 10) return;
    for (unsigned row = 0; row < 32; row++) for (unsigned col = 0; col < 32; col++) {
        unsigned offset = row * 32 + col;
        unsigned coverage = (metricGlyphs[index][offset / 4] >> ((offset % 4) * 2)) & 3;
        aaPixel(false, x + col, y + row, coverage, 7);
    }
}

void uiMeter(unsigned percent) {
    if (percent > 100) percent = 100;
    char value[5];
    snprintf(value, sizeof(value), "%u%%", percent);
    unsigned length = strlen(value);
    unsigned width = 0;
    for (unsigned index = 0; index < length; index++) width += metricWidths[value[index] == '%' ? 10 : value[index] - '0'];
    int x = (256 - (int)width) / 2;
    for (unsigned index = 0; index < length; index++) {
        largeDigit(x, 100, value[index]);
        x += metricWidths[value[index] == '%' ? 10 : value[index] - '0'];
    }
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
    prepareCoverageColors();
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

    vramSetBankB(VRAM_B_MAIN_SPRITE);
    oamInit(&oamMain, SpriteMapping_Bmp_1D_128, false);
    vramSetBankD(VRAM_D_SUB_SPRITE);
    oamInit(&oamSub, SpriteMapping_Bmp_1D_128, false);
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
